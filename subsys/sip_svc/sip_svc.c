/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Arm SiP services implementation.
 *
 *
 * ********
 * Overview
 * ********
 *
 * Typical flow
 *   (1) register a client, service returns a token
 *   (2) client open channel, only allow one channel at one time
 *   (3) client send request with callback, service returns transaction id
 *   (5) service callback once the transaction complete
 *   (6) client close channel after receive callback
 *   (7) ... repeats (2) to (7) to send more request
 *   (8) unregister the client
 *
 * Abort opened channel
 *   (1) for some reasons, client want to terminate the operation
 *       on the opened channel. client may close the channel
 *       without waiting for all transaction being completed
 *   (2) service will proceed to close the channel and set the client
 *       at ABORT state. The client will be not allowed to reopen
 *       the channel until service complete all its associated transactions
 *       and bring the client back to IDLE state.
 *
 * callback implementation requirement
 *   (1) the callback is provided by client, it will be called and executed
 *       in sip_svc thread once transaction is completed.
 *   (2) callback is requested to do the following:
 *       - if the client is running with a thread, callback should ensure
 *         the thread is still alive before handle the response
 *       - response data pointer is not retained after the callback function.
 *         thus, the callback should copy the response data when needed.
 *       - callback responsible to free the asynchronous response data memory
 *         space
 *
 *
 * ***************************************
 * sip_svc service and client overview
 * ***************************************
 * ------------------------------------------------------
 *                 Client1     Client2     Client3 ...
 * Support            |           *           |
 * multiple           |           * open      |
 * clients            |           * channel   |
 *                    |           *           |
 * ------------------------------------------------------
 * sip_svc
 * service
 * Thread
 *                ----------
 *                | Create | when receive first request
 *                ----------
 *                     |
 *                     | Run
 *                     |
 *                -------------------
 *            --> | Request handler | Process the request, perform smc/hvc
 *            |   -------------------
 *            |        |
 *    Resume  |        |
 *    when    |        |
 *    receive |   --------------------------
 *    new     |   | Async response handler | Poll response of async request
 *    request |   -------------------------- perform smc/hvc
 *            |        |
 *            |        | Suspend when all transactions
 *            |        | completed without new request
 *            |        |
 *            |   ------------------
 *            --- | Suspend Thread |
 *                ------------------
 * ------------------------------------------------------
 *
 * ***************************************
 * sip_svc service ID management
 * ***************************************
 * ------------------------------------------------------
 * client         Client                    Client
 *                   |                         |
 *                   | Register                | Send
 *                   |                         | Request
 *                   V                         V
 * ------------------------------------------------------
 * sip_svc            ^                        ^
 * service             | Client Token           | Transaction ID
 *                    |                        |
 *          ---------------------   -----------------------
 *          |  Alloc an client  |   | Alloc a Transaction |
 *          |  placeholder and  |   | ID for the request  |
 *          | generate a unique |   -----------------------
 *          |   token for it    |              |
 *          ---------------------              |
 *                                             |
 *                                             | Transaction ID
 *                                             V
 * ------------------------------------------------------
 * EL2/EL3                                      ^
 * firmware                                     |
 *                                   Return same Transaction ID
 * ------------------------------------------------------
 *
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sip_svc/sip_svc.h>
#include "sip_svc_id_mgr.h"

#define LOG_LEVEL CONFIG_ARM_SIP_SVC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sip_svc);

__weak bool sip_svc_plat_func_id_valid(uint32_t command, uint32_t func_id)
{
	if (command <= SIP_SVC_PROTO_CMD_MAX)
		return true;
	else
		return false;
}

__weak uint32_t sip_svc_plat_format_trans_id(uint32_t client_idx,
				uint32_t trans_idx)
{
	return (((client_idx & 0xF) << 4) | (trans_idx & 0xF));
}

__weak uint32_t sip_svc_plat_get_trans_idx(uint32_t trans_id)
{
	return (trans_id & 0xF);
}

__weak void sip_svc_plat_update_trans_id(struct sip_svc_request *request,
				uint32_t trans_id)
{
}

__weak void sip_svc_plat_free_async_memory(struct sip_svc_request *request)
{
}

__weak int sip_svc_plat_async_res_req(unsigned long *a0, unsigned long *a1,
				unsigned long *a2, unsigned long *a3,
				unsigned long *a4, unsigned long *a5,
				unsigned long *a6, unsigned long *a7,
				char *buf, size_t size)
{
	return -ENOTSUP;
}

__weak int sip_svc_plat_async_res_res(struct arm_smccc_res *res,
				char *buf, size_t *size,
				uint32_t *trans_id)
{
	return -ENOTSUP;
}


__weak uint32_t sip_svc_plat_get_error_code(struct arm_smccc_res *res)
{
	return -ENOTSUP;
}

#if CONFIG_ARM_SIP_SVC_HAS_HVC_CALLS
static void __invoke_fn_hvc(unsigned long function_id,
					  unsigned long arg0,
					  unsigned long arg1,
					  unsigned long arg2,
					  unsigned long arg3,
					  unsigned long arg4,
					  unsigned long arg5,
					  unsigned long arg6,
					  struct arm_smccc_res *res)
{
	arm_smccc_hvc(function_id, arg0, arg1, arg2, arg3,
		      arg4, arg5, arg6, res);
}
#endif

#if CONFIG_ARM_SIP_SVC_HAS_SMC_CALLS
static void __invoke_fn_smc(unsigned long function_id,
					  unsigned long arg0,
					  unsigned long arg1,
					  unsigned long arg2,
					  unsigned long arg3,
					  unsigned long arg4,
					  unsigned long arg5,
					  unsigned long arg6,
					  struct arm_smccc_res *res)
{
	arm_smccc_smc(function_id, arg0, arg1, arg2, arg3,
		      arg4, arg5, arg6, res);
}
#endif

static struct sip_svc_controller controller[] = {
#if CONFIG_ARM_SIP_SVC_HAS_SMC_CALLS
	{ .init = false, .method = "smc", .invoke_fn = __invoke_fn_smc },
#endif
#if CONFIG_ARM_SIP_SVC_HAS_HVC_CALLS
	{ .init = false, .method = "hvc", .invoke_fn = __invoke_fn_hvc },
#endif
	{.init = false, .method = "", .invoke_fn = NULL },
};

static uint32_t sip_svc_generate_c_token(void)
{
	uint32_t c_token = k_cycle_get_32();
	return c_token;
}

static uint32_t sip_svc_get_c_idx(struct sip_svc_controller *ctrl,
					 uint32_t c_token)
{
	uint32_t i;

	if (!ctrl)
		return SIP_SVC_ID_INVALID;

	for (i = 0; i < CONFIG_ARM_SIP_SVC_MAX_CLIENT_COUNT; i++) {
		if (ctrl->clients[i].token == c_token)
			return i;
	}

	return SIP_SVC_ID_INVALID;
}

uint32_t sip_svc_register(struct sip_svc_controller *ctrl, void *priv_data)
{
	uint32_t c_idx = SIP_SVC_ID_INVALID;

	if (!ctrl)
		return SIP_SVC_ID_INVALID;

	if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {
		c_idx = sip_svc_id_mgr_alloc(ctrl->client_id_pool);
		if (c_idx != SIP_SVC_ID_INVALID) {
			ctrl->clients[c_idx].id = c_idx;
			ctrl->clients[c_idx].token = sip_svc_generate_c_token();
			ctrl->clients[c_idx].state = SIP_SVC_CLIENT_ST_IDLE;
			ctrl->clients[c_idx].priv_data = priv_data;

			k_mutex_unlock(&ctrl->data_mutex);
			return ctrl->clients[c_idx].token;
		}
		k_mutex_unlock(&ctrl->data_mutex);
	}

	return SIP_SVC_ID_INVALID;
}

int sip_svc_unregister(struct sip_svc_controller *ctrl, uint32_t c_token)
{
	uint32_t c_idx;

	if (!ctrl)
		return -EINVAL;

	if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {

		c_idx = sip_svc_get_c_idx(ctrl, c_token);
		if (c_idx == SIP_SVC_ID_INVALID) {
			k_mutex_unlock(&ctrl->data_mutex);
			return -EINVAL;
		}
		if (ctrl->clients[c_idx].id == SIP_SVC_ID_INVALID) {
			k_mutex_unlock(&ctrl->data_mutex);
			return -ENODATA;
		}
		if (ctrl->clients[c_idx].active_trans_cnt != 0) {
			k_mutex_unlock(&ctrl->data_mutex);
			return -EBUSY;
		}

		if (ctrl->clients[c_idx].state == SIP_SVC_CLIENT_ST_OPEN) {
			ctrl->active_client_index = SIP_SVC_ID_INVALID;
			k_mutex_unlock(&ctrl->open_mutex);
		}

		ctrl->clients[c_idx].id = SIP_SVC_ID_INVALID;
		ctrl->clients[c_idx].state = SIP_SVC_CLIENT_ST_INVALID;
		ctrl->clients[c_idx].priv_data = NULL;
		sip_svc_id_mgr_free(ctrl->client_id_pool, c_idx);

		k_mutex_unlock(&ctrl->data_mutex);
	}

	return 0;
}

int sip_svc_open(struct sip_svc_controller *ctrl, uint32_t c_token, uint32_t timeout_us)
{
	k_timeout_t k_timeout;
	uint32_t c_idx;

	if (!ctrl)
		return -EINVAL;

	if (timeout_us == SIP_SVC_TIME_NO_WAIT)
		k_timeout = K_NO_WAIT;
	else if (timeout_us == SIP_SVC_TIME_FOREVER)
		k_timeout = K_FOREVER;
	else
		k_timeout = K_USEC(timeout_us);

	if (k_mutex_lock(&ctrl->open_mutex, k_timeout) == 0) {
		if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {
			c_idx = sip_svc_get_c_idx(ctrl, c_token);

			if (c_idx == SIP_SVC_ID_INVALID) {
				k_mutex_unlock(&ctrl->data_mutex);
				k_mutex_unlock(&ctrl->open_mutex);
				return -EINVAL;
			}

			if (ctrl->active_client_index != SIP_SVC_ID_INVALID) {
				k_mutex_unlock(&ctrl->data_mutex);
				k_mutex_unlock(&ctrl->open_mutex);
				return -EBUSY;
			}

			if (ctrl->clients[c_idx].state != SIP_SVC_CLIENT_ST_IDLE) {
				k_mutex_unlock(&ctrl->data_mutex);
				k_mutex_unlock(&ctrl->open_mutex);
				return -ENOTTY;
			}

			ctrl->active_client_index = c_idx;
			ctrl->clients[c_idx].state = SIP_SVC_CLIENT_ST_OPEN;

			k_mutex_unlock(&ctrl->data_mutex);
		}
	}

	return 0;
}

int sip_svc_close(struct sip_svc_controller *ctrl, uint32_t c_token)
{
	uint32_t c_idx;

	if (!ctrl)
		return -EINVAL;

	if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {
		c_idx = sip_svc_get_c_idx(ctrl, c_token);

		if (c_idx == SIP_SVC_ID_INVALID) {
			k_mutex_unlock(&ctrl->data_mutex);
			return -EINVAL;
		}

		if (ctrl->active_client_index == c_idx &&
		    ctrl->clients[c_idx].state == SIP_SVC_CLIENT_ST_OPEN) {
			if (ctrl->clients[c_idx].active_trans_cnt)
				ctrl->clients[c_idx].state = SIP_SVC_CLIENT_ST_ABORT;
			else
				ctrl->clients[c_idx].state = SIP_SVC_CLIENT_ST_IDLE;

			ctrl->active_client_index = SIP_SVC_ID_INVALID;

			k_mutex_unlock(&ctrl->data_mutex);
			k_mutex_unlock(&ctrl->open_mutex);
			return 0;
		}
		k_mutex_unlock(&ctrl->data_mutex);
	}

	return -EINVAL;
}

static void sip_svc_callback(struct sip_svc_controller *ctrl,
				uint32_t trans_id,
				char *sip_svc_response,
				uint32_t size)
{
	struct sip_svc_id_map_item *trans_id_item;
	uint64_t data_addr;
	uint64_t c_idx;

	if (!ctrl)
		return;

	if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {
		/* get trans id callback context from map */
		trans_id_item = sip_svc_id_map_query_item(
							ctrl->trans_id_map,
							trans_id);

		if (!trans_id_item) {
			k_mutex_unlock(&ctrl->data_mutex);
			return;
		}

		c_idx = (uint64_t)trans_id_item->arg6;

		ctrl->clients[c_idx].active_trans_cnt--;

		if (ctrl->clients[c_idx].state == SIP_SVC_CLIENT_ST_OPEN &&
		    trans_id_item->arg1) {

			((sip_svc_cb_fn)(trans_id_item->arg1))(
				ctrl->clients[c_idx].token,
				sip_svc_response, size);
		} else {
			/* free response memory space if
			 * callback is skipped
			 */
			data_addr = (
			   ((uint64_t) trans_id_item->arg2) << 32) |
			   ((uint64_t) trans_id_item->arg3);

			if (data_addr)
				k_free((char *)data_addr);
		}

		/* Free trans id */
		sip_svc_id_map_remove_item(ctrl->trans_id_map, trans_id);
		sip_svc_id_mgr_free(ctrl->clients[c_idx].trans_idx_pool,
					sip_svc_plat_get_trans_idx(trans_id));

		if (ctrl->clients[c_idx].active_trans_cnt != 0) {
			k_mutex_unlock(&ctrl->data_mutex);
			return;
		}

		if (ctrl->clients[c_idx].state == SIP_SVC_CLIENT_ST_ABORT) {
			ctrl->clients[c_idx].state = SIP_SVC_CLIENT_ST_IDLE;
		}

		k_mutex_unlock(&ctrl->data_mutex);
	}
}

static int sip_svc_request_handler(struct sip_svc_controller *ctrl)
{
	struct arm_smccc_res res;
	struct sip_svc_request request;
	struct sip_svc_response response;
	uint32_t trans_id;
	uint32_t cmd_code;
	uint32_t error_code;

	if (!ctrl)
		return -EINVAL;

	if (ctrl->active_job_cnt >= CONFIG_ARM_SIP_SVC_MAX_JOB_COUNT)
		return -EBUSY;

	if (k_mutex_lock(&ctrl->req_msgq_mutex, K_FOREVER) == 0) {
		if (k_msgq_num_used_get(&ctrl->req_msgq) == 0) {
			k_mutex_unlock(&ctrl->req_msgq_mutex);
			return 0;
		}
		if (k_msgq_get(&ctrl->req_msgq, &request, K_NO_WAIT) != 0) {
			k_mutex_unlock(&ctrl->req_msgq_mutex);
			return -EAGAIN;
		}
		k_mutex_unlock(&ctrl->req_msgq_mutex);
	}

	/* Get command code from request header */
	cmd_code = SIP_SVC_PROTO_HEADER_GET_CODE(request.header);

	/* Get trans_id from request header */
	trans_id = SIP_SVC_PROTO_HEADER_GET_TRANS_ID(request.header);

	/* Process the request, trigger smc/hvc call */
	if (cmd_code == SIP_SVC_PROTO_CMD_ASYNC)
		sip_svc_plat_update_trans_id(&request, trans_id);

	/* Increase active job count. Job means communication with
	 * secure monitor firmware
	 */
	ctrl->active_job_cnt++;

	/* Trigger smc/svc */
	LOG_DBG("before %s\n", ctrl->method);
	LOG_DBG("\theader         %08x\n", request.header);
	LOG_DBG("\ta0             %08lx\n", request.a0);
	LOG_DBG("\ta1             %08lx\n", request.a1);
	LOG_DBG("\ta2             %08lx\n", request.a2);
	LOG_DBG("\ta3             %08lx\n", request.a3);
	LOG_DBG("\ta4             %08lx\n", request.a4);
	LOG_DBG("\ta5             %08lx\n", request.a5);
	LOG_DBG("\ta6             %08lx\n", request.a6);
	LOG_DBG("\ta7             %08lx\n", request.a7);
	LOG_DBG("\tresp_data_addr %08llx\n", request.resp_data_addr);
	LOG_DBG("\tresp_data_size %d\n", request.resp_data_size);
	LOG_DBG("\tpriv_data      %p\n", request.priv_data);

	ctrl->invoke_fn(request.a0, request.a1,
			  request.a2, request.a3,
			  request.a4, request.a5,
			  request.a6, request.a7,
			  &res);

	LOG_DBG("after  %s\n", ctrl->method);
	LOG_DBG("\ta0             %08lx\n", res.a0);
	LOG_DBG("\ta1             %08lx\n", res.a1);
	LOG_DBG("\ta2             %08lx\n", res.a2);
	LOG_DBG("\ta3             %08lx\n", res.a3);

	/* Release async command data dynamic memory */
	if (cmd_code == SIP_SVC_PROTO_CMD_ASYNC)
		sip_svc_plat_free_async_memory(&request);

	/* Callback if fail or sync command */
	error_code = sip_svc_plat_get_error_code(&res);
	if (error_code != 0 || cmd_code == SIP_SVC_PROTO_CMD_SYNC) {
		response.header = SIP_SVC_PROTO_HEADER(error_code, trans_id);
		response.a0 = res.a0;
		response.a1 = res.a1;
		response.a2 = res.a2;
		response.a3 = res.a3;
		response.resp_data_addr = 0;
		response.resp_data_size = 0;
		response.priv_data = request.priv_data;

		sip_svc_callback(
			ctrl,
			trans_id,
			(char *)&response,
			sizeof(struct sip_svc_response));

		ctrl->active_job_cnt--;
	} else {
		ctrl->active_async_job_cnt++;
	}

	return -EINPROGRESS;
}

static int sip_svc_async_response_handler(struct sip_svc_controller *ctrl)
{
	struct sip_svc_id_map_item *trans_id_item;
	struct sip_svc_response response;
	uint32_t header;
	uint32_t trans_id;
	uint64_t data_addr;
	size_t data_size = CONFIG_ARM_SIP_SVC_MAX_ASYNC_RESP_SIZE;
	int ret;

	unsigned long a0 = 0;
	unsigned long a1 = 0;
	unsigned long a2 = 0;
	unsigned long a3 = 0;
	unsigned long a4 = 0;
	unsigned long a5 = 0;
	unsigned long a6 = 0;
	unsigned long a7 = 0;
	struct arm_smccc_res res;

	if (!ctrl) {
		return -EINVAL;
	}

	/* Return if no busy job id */
	if (ctrl->active_async_job_cnt == 0) {
		return 0;
	}

	if (sip_svc_plat_async_res_req(&a0, &a1, &a2, &a3, &a4, &a5, &a6, &a7,
		ctrl->async_resp_data, data_size)) {
		return -ENOTSUP;
	}

	/* Trigger SMC call */
	LOG_DBG("before %s (polling async response)\n", ctrl->method);
	LOG_DBG("\ta0             %08lx\n", a0);
	LOG_DBG("\ta1             %08lx\n", a1);
	LOG_DBG("\ta2             %08lx\n", a2);
	LOG_DBG("\ta3             %08lx\n", a3);
	LOG_DBG("\ta4             %08lx\n", a4);
	LOG_DBG("\ta5             %08lx\n", a5);
	LOG_DBG("\ta6             %08lx\n", a6);
	LOG_DBG("\ta7             %08lx\n", a7);

	ctrl->invoke_fn(a0, a1, a2, a3, a4, a5, a6, a7, &res);

	LOG_DBG("after  %s (polling async response)\n", ctrl->method);
	LOG_DBG("\ta0             %08lx\n", res.a0);
	LOG_DBG("\ta1             %08lx\n", res.a1);
	LOG_DBG("\ta2             %08lx\n", res.a2);
	LOG_DBG("\ta3             %08lx\n", res.a3);

	/* Callback if get response */
	ret = sip_svc_plat_async_res_res(&res,
			ctrl->async_resp_data, &data_size,
			&trans_id);

	if (ret == -ENOTSUP) {
		return 0;
	}

	if (ret != 0) {
		return -EINPROGRESS;
	}

	/* get caller information based on trans id */
	trans_id_item = sip_svc_id_map_query_item(ctrl->trans_id_map,
						     trans_id);

	if (!trans_id_item) {
		return -ENOENT;
	}

	header = SIP_SVC_PROTO_HEADER(sip_svc_plat_get_error_code(&res),
				      trans_id);

	/* Get caller provided memory space to put response */
	data_addr = (((uint64_t) trans_id_item->arg2 << 32) |
		     (uint64_t) trans_id_item->arg3);

	/* Check caller provided memory space to avoid overflow */
	if (data_size > ((size_t) trans_id_item->arg4))
		data_size = ((size_t)trans_id_item->arg4);

	response.header = header;
	response.a0 = res.a0;
	response.a1 = res.a1;
	response.a2 = res.a2;
	response.a3 = res.a3;
	response.resp_data_addr = data_addr;
	response.resp_data_size = data_size;
	response.priv_data = trans_id_item->arg5;

	/* Copy async cmd response into caller given memory space */
	if (data_addr)
		memcpy((char *)data_addr, ctrl->async_resp_data, data_size);

	sip_svc_callback(ctrl,
			    trans_id,
			    (char *)&response,
			    sizeof(struct sip_svc_response));

	/* Check again is there any async busy job id */
	ctrl->active_job_cnt--;
	ctrl->active_async_job_cnt--;
	if (ctrl->active_async_job_cnt == 0)
		return 0;

	return -EINPROGRESS;
}

static void sip_svc_thread(void *ctrl_ptr, void *arg2, void *arg3)
{
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	struct sip_svc_controller *ctrl = (struct sip_svc_controller *)ctrl_ptr;
	int ret_msgq;
	int ret_resp;

	while (1) {
		ret_msgq = -EINPROGRESS;
		ret_resp = -EINPROGRESS;
		while (ret_msgq != 0 || ret_resp != 0) {
			ret_msgq = sip_svc_request_handler(ctrl);
			ret_resp = sip_svc_async_response_handler(ctrl);
		}
		k_thread_suspend(ctrl->tid);
	}
}

int sip_svc_send(struct sip_svc_controller *ctrl,
		    uint32_t c_token,
		    char *sip_svc_request,
		    int size,
		    sip_svc_cb_fn cb)
{
	struct sip_svc_request *request =
		(struct sip_svc_request *)sip_svc_request;
	uint32_t trans_id = SIP_SVC_ID_INVALID;
	uint32_t trans_idx = SIP_SVC_ID_INVALID;
	uint32_t c_idx;

	if (!ctrl)
		return SIP_SVC_ID_INVALID;

	c_idx = sip_svc_get_c_idx(ctrl, c_token);
	if (c_idx == SIP_SVC_ID_INVALID)
		return SIP_SVC_ID_INVALID;

	if (ctrl->active_client_index != c_idx ||
	    ctrl->clients[c_idx].state != SIP_SVC_CLIENT_ST_OPEN)
		return SIP_SVC_ID_INVALID;

	if (!sip_svc_request || size != sizeof(struct sip_svc_request))
		return SIP_SVC_ID_INVALID;

	if (!sip_svc_plat_func_id_valid(SIP_SVC_PROTO_HEADER_GET_CODE(request->header),
					request->a0))
		return SIP_SVC_ID_INVALID;

	if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {

		/* Allocate a trans id for the request */
		trans_idx = sip_svc_id_mgr_alloc(ctrl->clients[c_idx].trans_idx_pool);
		if (trans_idx == SIP_SVC_ID_INVALID) {
			LOG_ERR("Fail to allocate transaction id");
			k_mutex_unlock(&ctrl->data_mutex);
			return SIP_SVC_ID_INVALID;
		}
		trans_id = sip_svc_plat_format_trans_id(c_idx, trans_idx);

		/* Assign the trans id of this request */
		SIP_SVC_PROTO_HEADER_SET_TRANS_ID(request->header, trans_id);

		/* Map trans id to client, callback, response data addr */
		if (sip_svc_id_map_insert_item(ctrl->trans_id_map,
			trans_id,
			(void *)cb,
			(void *)((request->resp_data_addr >> 32) & 0xFFFFFFFF),
			(void *)(request->resp_data_addr & 0xFFFFFFFF),
			(void *)(uint64_t)request->resp_data_size,
			request->priv_data,
			(void *)(uint64_t)c_idx) != 0) {

			LOG_ERR("Fail to insert transaction id to map");
			sip_svc_id_mgr_free(ctrl->clients[c_idx].trans_idx_pool,
						trans_idx);
			k_mutex_unlock(&ctrl->data_mutex);
			return SIP_SVC_ID_INVALID;
		}

		/* Insert request to MSGQ */
		if (k_mutex_lock(&ctrl->req_msgq_mutex, K_FOREVER) == 0) {

			if (k_msgq_put(&ctrl->req_msgq, sip_svc_request,
				K_NO_WAIT) != 0) {
				LOG_ERR("Request msgq full");
				sip_svc_id_map_remove_item(ctrl->trans_id_map, trans_id);
				sip_svc_id_mgr_free(ctrl->clients[c_idx].trans_idx_pool,
							trans_idx);
				k_mutex_unlock(&ctrl->req_msgq_mutex);
				k_mutex_unlock(&ctrl->data_mutex);
				return SIP_SVC_ID_INVALID;
			}
			k_mutex_unlock(&ctrl->req_msgq_mutex);
		}
		ctrl->clients[c_idx].active_trans_cnt++;

		if (!ctrl->tid) {
			LOG_ERR("Thread not spawned during init");
			sip_svc_id_map_remove_item(ctrl->trans_id_map, trans_id);
			sip_svc_id_mgr_free(ctrl->clients[c_idx].trans_idx_pool,
						trans_idx);
			k_mutex_unlock(&ctrl->data_mutex);
			return SIP_SVC_ID_INVALID;
		}
		k_thread_resume(ctrl->tid);
		k_mutex_unlock(&ctrl->data_mutex);
	}
	return trans_id;
}

void *sip_svc_get_priv_data(struct sip_svc_controller *ctrl,
			       uint32_t c_token)
{
	uint32_t c_idx;

	if (!ctrl)
		return NULL;

	if (k_mutex_lock(&ctrl->data_mutex, K_FOREVER) == 0) {
		c_idx = sip_svc_get_c_idx(ctrl, c_token);

		if (c_idx == SIP_SVC_ID_INVALID) {
			k_mutex_unlock(&ctrl->data_mutex);
			return NULL;
		}

		k_mutex_unlock(&ctrl->data_mutex);
		return ctrl->clients[c_idx].priv_data;
	}

	return NULL;
}

struct sip_svc_controller *sip_svc_get_controller(char *method)
{
	struct sip_svc_controller *cnt = controller;

	while (strlen(cnt->method) != 0) {
		if (!strcmp(cnt->method, method)) {
			return cnt;
		}
		cnt++;
	}
	return NULL;
}

static int sip_svc_init(const struct device *unused)
{
	ARG_UNUSED(unused);
	struct sip_svc_controller *ctrl = NULL;
	struct sip_svc_client *client = NULL;
	size_t msgq_size = sizeof(struct sip_svc_request) *
				CONFIG_ARM_SIP_SVC_MSGQ_DEPTH;
	char *msgq_buf = NULL;
	uint32_t i;
	int ret = 0;

	/* Get controller array */
	for (ctrl = &controller[0] ; ctrl->invoke_fn != NULL ; ctrl++) {
		/* Return if controller has been initialized */
		if (ctrl->init)
			continue;

		ctrl->client_id_pool = sip_svc_id_mgr_create(
						CONFIG_ARM_SIP_SVC_MAX_CLIENT_COUNT);
		if (!ctrl->client_id_pool)
			return -ENOMEM;


		ctrl->trans_id_map = sip_svc_id_map_create(
					CONFIG_ARM_SIP_SVC_MSGQ_DEPTH);
		if (!ctrl->trans_id_map) {
			sip_svc_id_mgr_delete(ctrl->client_id_pool);
			return -ENOMEM;
		}

		/* Alloc request msgq ring buffer */
		msgq_buf = k_malloc(msgq_size);
		if (!msgq_buf) {
			sip_svc_id_mgr_delete(ctrl->client_id_pool);
			sip_svc_id_map_delete(ctrl->trans_id_map);
			return -ENOMEM;
		}

		/* Initialize request msgq */
		k_msgq_init(&ctrl->req_msgq, msgq_buf,
				sizeof(struct sip_svc_request),
				CONFIG_ARM_SIP_SVC_MSGQ_DEPTH);

		/* Initialize request msgq mutex */
		k_mutex_init(&ctrl->req_msgq_mutex);

		/* Reset thread pointer */
		ctrl->tid = NULL;

		/* Initialize client contents */
		for (i = 0; i < CONFIG_ARM_SIP_SVC_MAX_CLIENT_COUNT; i++) {
			client = &ctrl->clients[i];
			client->id = SIP_SVC_ID_INVALID;
			client->token = SIP_SVC_ID_INVALID;
			client->state = SIP_SVC_CLIENT_ST_INVALID;
			client->active_trans_cnt = 0;

			client->trans_idx_pool = sip_svc_id_mgr_create(
						CONFIG_ARM_SIP_SVC_MAX_TRANSACTION_COUNT);
			if (!client->trans_idx_pool) {
				ret = -ENOMEM;
				break;
			}
		}

		if (ret != 0) {
			sip_svc_id_mgr_delete(ctrl->client_id_pool);
			k_free(ctrl->trans_id_map);
			k_free(msgq_buf);

			for (i = 0; i < CONFIG_ARM_SIP_SVC_MAX_CLIENT_COUNT; i++) {
				client = &ctrl->clients[i];
				if (client->trans_idx_pool)
					sip_svc_id_mgr_delete(client->trans_idx_pool);
			}
			return ret;
		}

		/* Create and run the thread */
		ctrl->tid = k_thread_create(&ctrl->thread, ctrl->stack,
					CONFIG_ARM_SIP_SVC_THREAD_STACK_SIZE,
					sip_svc_thread, ctrl, NULL, NULL,
					CONFIG_ARM_SIP_SVC_THREAD_PRIORITY,
					0, K_NO_WAIT);

		ctrl->active_client_index = SIP_SVC_ID_INVALID;
		ctrl->active_job_cnt = 0;
		ctrl->active_async_job_cnt = 0;

		/* Initialize mutex */
		k_mutex_init(&ctrl->open_mutex);
		k_mutex_init(&ctrl->data_mutex);

		ctrl->init = true;
	}
	return 0;
}

SYS_INIT(sip_svc_init, POST_KERNEL, CONFIG_ARM_SIP_SVC_INIT_PRIORITY);
