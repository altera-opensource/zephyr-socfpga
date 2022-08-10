/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SERVICE_SIP_SVC_H_
#define ZEPHYR_INCLUDE_SERVICE_SIP_SVC_H_

/**
 * @file
 * @brief Public API for Arm SiP services
 *
 * Arm SiP service provides the capability to send the
 * SMC/HVC call from kernel running at EL1 to hypervisor/secure
 * monitor firmware running at EL2/EL3.
 *
 * Only allow one SMC and one HVC per system.
 *
 * The service support multiple clients.
 *
 * The client must open a channel before sending any request and
 * close the channel immediately after complete. The service only
 * allow one channel at one time.
 *
 * The service will return the SMC/HVC return value to the client
 * via callback function.
 *
 * The client state machine
 * - IDLE  : Initial state.
 * - OPEN  : The client will switch from IDLE to OPEN once it
 *           successfully open the channel. On the other hand, it
 *           will switch from OPEN to IDLE state once it successfully
 *           close the channel.
 * - ABORT : The client has closed the channel, however, there are
 *           incomplete transactions being left over. The service
 *           will only move the client back to IDLE state once all
 *           transactions completed. The client is not allowed to
 *           re-open the channel when in ABORT state/
 */

#include <arch/arm64/arm-smccc.h>
#include <sip_svc/sip_svc_proto.h>

#define SIP_SVC_CLIENT_ST_INVALID	0
#define SIP_SVC_CLIENT_ST_IDLE		1
#define SIP_SVC_CLIENT_ST_OPEN		2
#define SIP_SVC_CLIENT_ST_ABORT		3

#define SIP_SVC_ID_INVALID		0xFFFFFFFF

#define SIP_SVC_TIME_NO_WAIT		0
#define SIP_SVC_TIME_FOREVER		0xFFFFFFFF

#define SIP_SVC_NAME_LENGTH		4

typedef void (sip_svc_fn)(unsigned long, unsigned long,
			  unsigned long, unsigned long,
			  unsigned long, unsigned long,
			  unsigned long, unsigned long,
			  struct arm_smccc_res *);

/** @brief Arm SiP Services client data.
 */
struct sip_svc_client {

	uint32_t id;
	uint32_t token;
	uint32_t state;
	uint32_t active_trans_cnt;
	void *priv_data;

	struct sip_svc_id_pool *trans_idx_pool;
};

/** @brief Arm SiP Services controller data.
 */
struct sip_svc_controller {

	bool init;
	char method[SIP_SVC_NAME_LENGTH];
	sip_svc_fn *invoke_fn;

	struct sip_svc_id_pool *client_id_pool;
	struct sip_svc_id_map *trans_id_map;

	struct k_mutex req_msgq_mutex;
	struct k_msgq req_msgq;

	k_tid_t tid;
	struct k_thread	thread;

	K_KERNEL_STACK_MEMBER(stack, CONFIG_ARM_SIP_SVC_THREAD_STACK_SIZE);

	struct k_mutex open_mutex;
	struct k_mutex data_mutex;
	struct sip_svc_client clients[CONFIG_ARM_SIP_SVC_MAX_CLIENT_COUNT];
	uint32_t active_client_index;

	uint32_t active_job_cnt;
	uint32_t active_async_job_cnt;
	uint8_t async_resp_data[CONFIG_ARM_SIP_SVC_MAX_ASYNC_RESP_SIZE];
};

/** @brief ARM sip service callback function prototype for response after completion
 *
 * On success , response is returned via a callback to the user.
 *
 * @param c_token Client's token
 * @param res pointer to response buffer
 * @param size Data size in res address
 */
typedef void (*sip_svc_cb_fn)(uint32_t c_token, char *res, int size);

/**
 * @brief Register a client on Arm SiP service
 *
 * On success, the client will be at IDLE state in the service and
 * the service will return a token to the client. The client can then
 * use the token to open the channel on the service and communicate
 * with hypervisor/secure monitor firmware running at EL2/EL3.
 *
 * @param ctrl controller handler structure whose service provides Arm SMC/HVC
 *            SiP services.
 * @param priv_data Pointer to client private data.
 * @return token on success, 0xFFFFFFFF on failure.
 */
uint32_t sip_svc_register(struct sip_svc_controller *ctrl,
			     void *priv_data);

/**
 * @brief Unregister a client on Arm SiP service
 *
 * On success, detach the client from the service. Unregistration
 * is only allowed when all transactions belong to the client are closed.
 *
 * @param ctrl controller handler structure which provides Arm SiP services.
 * @param c_token Client's token
 * @return 0 on success, negative errno on failure.
 */
int sip_svc_unregister(struct sip_svc_controller *ctrl,
			  uint32_t c_token);

/**
 * @brief Client requests to open a channel on Arm SiP service.
 *
 * Client must open a channel on the before sending any request via
 * SMC/HVC to hypervisor/secure monitor firmware running at EL2/EL3.
 *
 * The service only allow one opened channel at one time and it is protected
 * by mutex.
 *
 * @param ctrl Controller handler structure which provides Arm SiP services.
 * @param c_token Client's token
 * @param timeout_us Waiting time if the mutex have been locked.
 *                   When the mutex have been locked:
 *                   - returns non-zero error code immediately if value is 0
 *                   - wait forever if the value is 0xFFFFFFFF
 *                   - otherwise, for the given time
 *
 * @return 0 on success, negative errno on failure.
 */
int sip_svc_open(struct sip_svc_controller *ctrl,
		    uint32_t c_token, uint32_t timeout_us);

/**
 * @brief Client requests to close the channel on Arm SiP services.
 *
 * Client must close the channel immediately once complete.
 *
 * @param ctrl Controller handler structure which provides Arm SiP services.
 * @param c_token Client's token
 *
 * @return 0 on success, negative errno on failure.
 */
int sip_svc_close(struct sip_svc_controller *ctrl,
		     uint32_t c_token);

/**
 * @brief Client requests to send a SMC/HVC call to EL3/EL2
 *
 * Client must open a channel on the device before using this function.
 * This function is non-blocking and can be called from any context. The
 * service will return a Transaction ID to the client if the request
 * is being accepted. Client callback is called when the transaction is
 * completed.
 *
 * @param ctrl Controller structure which provides Arm SiP services.
 * @param c_token Client's token
 * @param req Address to the user input in struct sip_svc_request format.
 * @param size Data size in 'req' address.
 * @param cb Callback. SMC/SVC return value will be passed to client via
 *           context in struct sip_svc_response format in callback.
 *
 * @return 0 on success, negative errno on failure.
 */
int sip_svc_send(struct sip_svc_controller *ctrl,
		    uint32_t c_token,
		    char *req,
		    int size,
		    sip_svc_cb_fn cb);

/**
 * @brief Get the address pointer to the client private data.
 *
 * The pointer is provided by client during registration.
 *
 * @param ctrl Controller structure which provides Arm SiP services.
 * @param c_token Client's token
 *
 * @return address pointer to the client private data
 */
void *sip_svc_get_priv_data(struct sip_svc_controller *ctrl,
			       uint32_t c_token);

/**
 * @brief get the Arm SiP service handle
 *
 * @param method Controller structure which provides Arm SiP services.
 *
 * @return valid pointer or NULL.
 */
struct sip_svc_controller *sip_svc_get_controller(char *method);

#endif /* ZEPHYR_INCLUDE_SERVICE_SIP_SVC_H_ */
