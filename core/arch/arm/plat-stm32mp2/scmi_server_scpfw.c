// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2024, STMicroelectronics International N.V.
 */

#include <drivers/mailbox.h>
#include <drivers/scmi.h>
#include <drivers/scmi-msg.h>
#include <drivers/stm32_cpu_opp.h>
#include <drivers/stm32_remoteproc.h>
#include <drivers/stm32mp_dt_bindings.h>
#include <kernel/boot.h>
#include <kernel/dt.h>
#include <kernel/dt_driver.h>
#include <kernel/notif.h>
#include <libfdt.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <scmi_clock_consumer.h>
#include <scmi_pd_consumer.h>
#include <scmi_regulator_consumer.h>
#include <scmi_reset_consumer.h>
#include <scmi/scmi_server.h>
#include <tee_api_types.h>
#include <tee_api_defines.h>
#include <tee/cache.h>

/*
 * struct optee_scmi_server - Data of scmi_server_scpfw
 *
 * @dt_name: SCMI node name
 * @agent_list: list of optee_scmi_server_agent
 */
struct optee_scmi_server {
	const char *dt_name;

	SIMPLEQ_HEAD(agent_list_head, optee_scmi_server_agent) agent_list;
};

/*
 * @struct optee_scmi_server_agent - Data of an SCMI agent
 *
 * @dt_name: SCMI agent node name
 * @agent_id: SCMI agent identifier
 * @channel_id: SCMI channel identifier
 * @scp_mailbox_idx: SCPFW mailbox identifier
 * @mbox_chan: handle for a mailbox channel
 * @event_waiting: an event from mailbox is waiting to be handle
 * @mailbox_notif: data for threaded execution
 * @shm: shared memory information
 * @exposed_clock: data for clocks exposed thru SCMI
 * @protocol_list: list of optee_scmi_server_protocol
 * @link: link for optee_scmi_server:agent_list
 */
struct optee_scmi_server_agent {
	const char *dt_name;
	unsigned int agent_id;
	unsigned int channel_id;
	unsigned int scp_mailbox_idx;
	struct mbox_chan *mbox_chan;
	bool event_waiting;
	struct notif_driver mailbox_notif;
	struct shared_mem shm;

	SIMPLEQ_HEAD(protocol_list_head, optee_scmi_server_protocol)
		protocol_list;

	SIMPLEQ_ENTRY(optee_scmi_server_agent) link;
};

/*
 * struct optee_scmi_server_protocol - Data of an SCMI protocol
 *
 * @dt_name: SCMI protocol node name
 * @dt_node: SCMI protocol node
 * @protocol_id: SCMI protocol identifier
 * @link: list for optee_scmi_server_agent:protocol_list
 */
struct optee_scmi_server_protocol {
	const char *dt_name;
	int dt_node;
	unsigned int protocol_id;

	SIMPLEQ_ENTRY(optee_scmi_server_protocol) link;
};

/* scmi_agent_configuration API */
static struct scpfw_config scpfw_cfg;

static void scmi_scpfw_free_agent(struct scpfw_agent_config agent_cfg)
{
	unsigned int j = 0;
	unsigned int k = 0;

	for (j = 0; j < agent_cfg.channel_count; j++) {
		struct scpfw_channel_config *channel_cfg =
			agent_cfg.channel_config + j;

		for (k = 0; k < channel_cfg->perfd_count; k++) {
			struct scmi_perfd *perfd = channel_cfg->perfd + k;

			free(perfd->dvfs_opp_khz);
			free(perfd->dvfs_opp_mv);
		}
		free(channel_cfg->perfd);

		free(channel_cfg->pd);
		free(channel_cfg->voltd);
		free(channel_cfg->reset);
		free(channel_cfg->clock);
	}
	free(agent_cfg.channel_config);
}

struct scpfw_config *scmi_scpfw_get_configuration(void)
{
	struct scpfw_agent_config *old_agent_config = scpfw_cfg.agent_config;

	assert(scpfw_cfg.agent_count >= 1);

	/*
	 * Do not expose agent_config[0] as it is empty
	 * and SCP didn't want it.
	 */
	scpfw_cfg.agent_count--;
	scpfw_cfg.agent_config = calloc(scpfw_cfg.agent_count,
					sizeof(*scpfw_cfg.agent_config));

	memcpy(scpfw_cfg.agent_config, old_agent_config + 1,
	       sizeof(*scpfw_cfg.agent_config) * scpfw_cfg.agent_count);

	scmi_scpfw_free_agent(old_agent_config[0]);
	free(old_agent_config);

	return &scpfw_cfg;
}

void scmi_scpfw_release_configuration(void)
{
	unsigned int i = 0;

	for (i = 0; i < scpfw_cfg.agent_count; i++)
		scmi_scpfw_free_agent(scpfw_cfg.agent_config[i]);

	free(scpfw_cfg.agent_config);
}

static void yielding_mailbox_notif(struct notif_driver *ndrv,
				   enum notif_event ev)
{
	struct optee_scmi_server_agent *ctx = container_of(ndrv,
		struct optee_scmi_server_agent, mailbox_notif);

	if (ev == NOTIF_EVENT_DO_BOTTOM_HALF && ctx->event_waiting) {
		ctx->event_waiting = false;

		/* Ack notification */
		if (mbox_recv(ctx->mbox_chan, false, NULL, 0))
			panic();

		/* Let SCP handle the message and the answer */
		if (scmi_server_smt_process_thread(ctx->scp_mailbox_idx)) {
			/*
			 * It should force the SMT_CHANNEL_STATUS_ERROR in the
			 * mailbox header channel_status_field and notify the
			 * Cortex-M the response sent.
			 * For now just panic().
			 */
			panic();
		}

		/* Notify requester that an answer is ready */
		if (mbox_send(ctx->mbox_chan, false, NULL, 0))
			panic();
	}
}

static void mailbox_rcv_callback(void *cookie)
{
	struct optee_scmi_server_agent *ctx = cookie;

	ctx->event_waiting = true;
	notif_send_async(NOTIF_VALUE_DO_BOTTOM_HALF);
}

static TEE_Result optee_scmi_server_shm_map(struct shared_mem *shm)
{
	/* Nothing to do */
	if (!shm->va && !shm->size)
		return TEE_SUCCESS;

	/* Only consider non-secure client for now */
	shm->va = (vaddr_t)core_mmu_add_mapping(MEM_AREA_IO_NSEC,
						shm->pa, shm->size);

	if (!shm->va)
		return TEE_ERROR_BAD_PARAMETERS;

	return TEE_SUCCESS;
}

static TEE_Result optee_scmi_server_probe_agent(const void *fdt, int agent_node,
						struct optee_scmi_server_agent
							*agent_ctx)
{
	struct optee_scmi_server_protocol *protocol_ctx = NULL;
	int protocol_node = 0;
	uint32_t phandle = 0;
	int shmem_node = 0;
	const fdt32_t *cuint = NULL;
	TEE_Result res = TEE_SUCCESS;

	SIMPLEQ_INIT(&agent_ctx->protocol_list);

	/* Get agent id (required) */
	cuint = fdt_getprop(fdt, agent_node, "reg", NULL);
	if (!cuint) {
		EMSG("%s Missing property reg", agent_ctx->dt_name);
		panic();
	}
	agent_ctx->agent_id = fdt32_to_cpu(*cuint);

	/* Agent id 0 is stritly reserved to SCMI server. */
	assert(agent_ctx->agent_id > 0);

	if (!fdt_node_check_compatible(fdt, agent_node, "arm,scmi")) {
		res = mbox_dt_register_chan(mailbox_rcv_callback, NULL,
					    agent_ctx, fdt, agent_node,
					    &agent_ctx->mbox_chan);
		if (res == TEE_ERROR_DEFER_DRIVER_INIT) {
			EMSG("%s Mailbox request an impossible probe defer",
			     protocol_ctx->dt_name);
			panic();
		} else if (res) {
			EMSG("%s Failed to register mailbox channel",
			     protocol_ctx->dt_name);
			panic();
		}
	} else if (!fdt_node_check_compatible(fdt, agent_node,
					      "linaro,scmi-optee")) {
		cuint = fdt_getprop(fdt, agent_node, "scmi-channel-id", NULL);
		if (!cuint) {
			EMSG("%s scmi-channel-id property not found",
			     agent_ctx->dt_name);
			panic();
		}
		agent_ctx->channel_id = fdt32_to_cpu(*cuint);
	} else {
		EMSG("%s Incorrect compatible", agent_ctx->dt_name);
		panic();
	}

	/* Manage shmem property (optional) */
	cuint = fdt_getprop(fdt, agent_node, "shmem", NULL);
	if (cuint) {
		phandle = fdt32_to_cpu(*cuint);
		shmem_node = fdt_node_offset_by_phandle(fdt, phandle);
		agent_ctx->shm.size = fdt_reg_size(fdt, shmem_node);
		agent_ctx->shm.pa = fdt_reg_base_address(fdt, shmem_node);
		if (shmem_node >= 0 &&
		    (agent_ctx->shm.pa == DT_INFO_INVALID_REG ||
		     agent_ctx->shm.size == DT_INFO_INVALID_REG_SIZE)) {
			panic();
		}

		optee_scmi_server_shm_map(&agent_ctx->shm);
	}

	fdt_for_each_subnode(protocol_node, fdt, agent_node) {
		const char *node_name = fdt_get_name(fdt, protocol_node, NULL);
		struct optee_scmi_server_protocol *p = NULL;

		if (!strstr(node_name, "protocol@"))
			continue;

		protocol_ctx = calloc(1, sizeof(*protocol_ctx));
		if (!protocol_ctx)
			return TEE_ERROR_OUT_OF_MEMORY;

		protocol_ctx->dt_name = node_name;
		protocol_ctx->dt_node = protocol_node;

		/* Get protocol id (required) */
		cuint = fdt_getprop(fdt, protocol_node, "reg", NULL);
		if (!cuint) {
			EMSG("%s Missing property reg", protocol_ctx->dt_name);
			panic();
		}
		protocol_ctx->protocol_id = fdt32_to_cpu(*cuint);

		SIMPLEQ_FOREACH(p, &agent_ctx->protocol_list, link)
			assert(p->protocol_id != protocol_ctx->protocol_id);

		SIMPLEQ_INSERT_TAIL(&agent_ctx->protocol_list, protocol_ctx,
				    link);
	}

	return TEE_SUCCESS;
}

static TEE_Result optee_scmi_server_probe(const void *fdt, int parent_node,
					  const void *compat_data __unused)
{
	struct optee_scmi_server *ctx = NULL;
	struct optee_scmi_server_agent *agent_ctx = NULL;
	struct optee_scmi_server_agent *a = NULL;
	TEE_Result res = TEE_SUCCESS;
	unsigned int agent_cfg_count = 0;
	unsigned int scp_mailbox_idx = 0;
	unsigned int i = 0;
	int agent_node = 0;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return TEE_ERROR_OUT_OF_MEMORY;

	ctx->dt_name = fdt_get_name(fdt, parent_node, NULL);

	/* Read device tree */
	SIMPLEQ_INIT(&ctx->agent_list);

	fdt_for_each_subnode(agent_node, fdt, parent_node) {
		const char *node_name = fdt_get_name(fdt, agent_node, NULL);

		if (!strstr(node_name, "agent@"))
			continue;

		agent_ctx = calloc(1, sizeof(*agent_ctx));
		if (!agent_ctx) {
			res = TEE_ERROR_OUT_OF_MEMORY;
			goto fail_agent;
		}
		agent_ctx->dt_name = node_name;

		res = optee_scmi_server_probe_agent(fdt, agent_node, agent_ctx);
		if (res)
			goto fail_agent;

		SIMPLEQ_FOREACH(a, &ctx->agent_list, link)
			assert(a->agent_id != agent_ctx->agent_id);

		SIMPLEQ_INSERT_TAIL(&ctx->agent_list, agent_ctx, link);
		agent_cfg_count = MAX(agent_cfg_count, agent_ctx->agent_id);
	}

	agent_cfg_count++;

	/* Create SCMI config structures */
	scpfw_cfg.agent_count = agent_cfg_count;
	scpfw_cfg.agent_config = calloc(scpfw_cfg.agent_count,
					sizeof(*scpfw_cfg.agent_config));
	if (!scpfw_cfg.agent_config) {
		res = TEE_ERROR_OUT_OF_MEMORY;
		scpfw_cfg.agent_count = 0;
		goto fail_agent;
	}

	SIMPLEQ_FOREACH(agent_ctx, &ctx->agent_list, link) {
		struct scpfw_agent_config *agent_cfg =
			scpfw_cfg.agent_config + agent_ctx->agent_id;

		agent_cfg->name = agent_ctx->dt_name;
		agent_cfg->agent_id = agent_ctx->agent_id;

		if (agent_ctx->mbox_chan) {
			agent_ctx->event_waiting = false;
			agent_ctx->mailbox_notif.yielding_cb =
				yielding_mailbox_notif;
			notif_register_driver(&agent_ctx->mailbox_notif);
		}

		/*
		 * Right now this driver can handle one channel per agent only.
		 */
		assert(agent_ctx->channel_id == 0);
		agent_cfg->channel_count = 1;
		agent_cfg->channel_config =
			calloc(agent_cfg->channel_count,
			       sizeof(*agent_cfg->channel_config));
		if (!agent_cfg->channel_config) {
			res = TEE_ERROR_OUT_OF_MEMORY;
			agent_cfg->channel_count = 0;
			goto fail_scpfw_cfg;
		}

		for (i = 0; i < agent_cfg->channel_count; i++) {
			struct scpfw_channel_config *channel_cfg =
				agent_cfg->channel_config + i;

			channel_cfg->name = "channel";
			channel_cfg->channel_id = agent_ctx->channel_id;
			channel_cfg->mailbox_idx = scp_mailbox_idx++;
			channel_cfg->shm = agent_ctx->shm;

			/* One channel per agent only */
			agent_ctx->scp_mailbox_idx = channel_cfg->mailbox_idx;
		}

	}

	/* Parse protocols and fill channels config */
	SIMPLEQ_FOREACH(agent_ctx, &ctx->agent_list, link) {
		struct optee_scmi_server_protocol *protocol_ctx = NULL;
		struct scpfw_agent_config *agent_cfg =
			scpfw_cfg.agent_config + agent_ctx->agent_id;
		struct scpfw_channel_config *channel_cfg =
			agent_cfg->channel_config + agent_ctx->channel_id;

		res = optee_scmi_server_init_dvfs(fdt, 0, agent_cfg,
						  channel_cfg);
		if (res)
			panic("Error during dvfs init");

		SIMPLEQ_FOREACH(protocol_ctx, &agent_ctx->protocol_list, link) {
			switch (protocol_ctx->protocol_id) {
			case SCMI_PROTOCOL_ID_POWER_DOMAIN:
				res = optee_scmi_server_init_pd(fdt,
					protocol_ctx->dt_node,
					agent_cfg, channel_cfg);
				if (res)
					panic("Error during power domains init"
					      );
				break;
			case SCMI_PROTOCOL_ID_CLOCK:
				res = optee_scmi_server_init_clocks(fdt,
					protocol_ctx->dt_node, agent_cfg,
					channel_cfg);
				if (res)
					panic("Error during clocks init");
				break;
			case SCMI_PROTOCOL_ID_VOLTAGE_DOMAIN:
				res = optee_scmi_server_init_regulators(fdt,
					protocol_ctx->dt_node, agent_cfg,
					channel_cfg);
				if (res)
					panic("Error during regulators init");
				break;
			case SCMI_PROTOCOL_ID_RESET_DOMAIN:
				res = optee_scmi_server_init_resets(fdt,
					protocol_ctx->dt_node, agent_cfg,
					channel_cfg);
				if (res)
					panic("Error during resets init");
				break;
			default:
				EMSG("%s Unknown protocol ID: %#x",
				     protocol_ctx->dt_name,
				     protocol_ctx->protocol_id);
				panic();
				break;
			}
		}
		i++;
	}

	return TEE_SUCCESS;

fail_scpfw_cfg:
	scmi_scpfw_release_configuration();

fail_agent:
	while (!SIMPLEQ_EMPTY(&ctx->agent_list)) {
		agent_ctx = SIMPLEQ_FIRST(&ctx->agent_list);

		while (!SIMPLEQ_EMPTY(&agent_ctx->protocol_list)) {
			struct optee_scmi_server_protocol *protocol_ctx =
				SIMPLEQ_FIRST(&agent_ctx->protocol_list);

			SIMPLEQ_REMOVE_HEAD(&agent_ctx->protocol_list, link);
			free(protocol_ctx);
		}

		SIMPLEQ_REMOVE_HEAD(&ctx->agent_list, link);
		free(agent_ctx);
	}

	free(ctx);

	return res;
}

static TEE_Result optee_scmi_server_init(void)
{
	const void *fdt = get_embedded_dt();
	int node = -1;

	if (!fdt)
		panic();

	node = fdt_node_offset_by_compatible(fdt, node, "optee,scmi-server");
	if (node == -FDT_ERR_NOTFOUND)
		panic();

	return optee_scmi_server_probe(fdt, node, NULL);
}
driver_init_late(optee_scmi_server_init);
