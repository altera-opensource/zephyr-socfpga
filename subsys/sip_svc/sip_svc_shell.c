/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Arm SiP service shell command 'sip_svc'.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/sip_svc/sip_svc.h>
#include <stdlib.h>

struct private_data {
	struct k_sem semaphore;
	const struct shell *sh;
};

static int parse_common_args(const struct shell *sh, char **argv,
			     struct sip_svc_controller **ctrl)
{
	*ctrl = sip_svc_get_controller(argv[1]);
	if (!*ctrl) {
		shell_error(sh, "service %s not found", argv[1]);
		return -ENODEV;
	} else if (!(*ctrl)->init) {
		shell_error(sh, "Arm SiP services method %s not initialized", argv[1]);
		return -ENODEV;
	}
	return 0;
}

static int cmd_reg(const struct shell *sh, size_t argc, char **argv)
{
	struct sip_svc_controller *ctrl;
	uint32_t c_token;
	int err;

	err = parse_common_args(sh, argv, &ctrl);
	if (err < 0) {
		return err;
	}

	c_token = sip_svc_register(ctrl, NULL);
	if (c_token == SIP_SVC_ID_INVALID) {
		shell_error(sh, "%s: register fail", ctrl->method);
		err = -1;
	} else {
		shell_print(sh, "%s: register success: client token %08x\n",
			    ctrl->method, c_token);
		err = 0;
	}

	return err;
}

static int cmd_unreg(const struct shell *sh, size_t argc, char **argv)
{
	struct sip_svc_controller *ctrl;
	uint64_t c_token;
	int err;
	char *ptr;

	err = parse_common_args(sh, argv, &ctrl);
	if (err < 0) {
		return err;
	}

	c_token = strtol(argv[2], &ptr, 16);

	err = sip_svc_unregister(ctrl, (uint32_t)c_token);
	if (err) {
		shell_error(sh, "%s: unregister fail (%d): client token %08x",
			    ctrl->method, err, (uint32_t)c_token);
	} else {
		shell_print(sh, "%s: unregister success: client token %08x",
			    ctrl->method, (uint32_t)c_token);
	}

	return err;
}

static int cmd_open(const struct shell *sh, size_t argc, char **argv)
{
	struct sip_svc_controller *ctrl;
	uint64_t c_token;
	uint32_t usecond = 0;
	int err;
	char *ptr;

	err = parse_common_args(sh, argv, &ctrl);
	if (err < 0) {
		return err;
	}

	c_token = strtol(argv[2], &ptr, 16);

	if (argc > 3) {
		usecond = (uint32_t)atoi(argv[3]) * 1000000;
	}

	err = sip_svc_open(ctrl, (uint32_t)c_token, usecond);
	if (err) {
		shell_error(sh, "%s: open fail (%d): client token %08x",
			    ctrl->method, err, (uint32_t)c_token);
	} else {
		shell_print(sh, "%s: open success: client token %08x",
			    ctrl->method, (uint32_t)c_token);
	}

	return err;
}

static int cmd_close(const struct shell *sh, size_t argc, char **argv)
{
	struct sip_svc_controller *ctrl;
	uint64_t c_token;
	int err;
	char *ptr;

	err = parse_common_args(sh, argv, &ctrl);
	if (err < 0) {
		return err;
	}

	c_token = strtol(argv[2], &ptr, 16);

	err = sip_svc_close(ctrl, (uint32_t)c_token);
	if (err) {
		shell_error(sh, "%s: close fail (%d): client token %08x",
			    ctrl->method, err, (uint32_t)c_token);
	} else {
		shell_print(sh, "%s: close success: client token %08x",
			    ctrl->method, (uint32_t)c_token);
	}

	return err;
}

static void cmd_send_callback(uint32_t c_token, char *res, int size)
{
	struct sip_svc_response *response =
		(struct sip_svc_response *) res;

	struct private_data *priv = (struct private_data *)response->priv_data;
	const struct shell *sh = priv->sh;

	shell_print(sh, "\n\rsip_svc send callback response\n");

	if ((size_t)size != sizeof(struct sip_svc_response)) {
		shell_error(sh, "Invalid response size (%d), expects (%ld)",
			size, sizeof(struct sip_svc_response));
		return;
	}

	shell_print(sh, "\theader=%08x\n", response->header);
	shell_print(sh, "\ta0=%016lx\n", response->a0);
	shell_print(sh, "\ta1=%016lx\n", response->a1);
	shell_print(sh, "\ta2=%016lx\n", response->a2);
	shell_print(sh, "\ta3=%016lx\n", response->a3);

	k_sem_give(&(priv->semaphore));
}

static int cmd_send(const struct shell *sh, size_t argc, char **argv)
{
	struct sip_svc_controller *ctrl;
	uint64_t c_token;
	int trans_id;
	struct sip_svc_request request;
	struct private_data *priv = NULL;
	int err;
	char *ptr;

	err = parse_common_args(sh, argv, &ctrl);
	if (err < 0)
		return err;

	priv = (struct private_data *)k_malloc(sizeof(struct private_data));
	if (!priv) {
		shell_error(sh, "Fail to allocate private variable memory");
		return -ENOMEM;
	}

	c_token = strtol(argv[2], &ptr, 16);

	request.header = SIP_SVC_PROTO_HEADER(SIP_SVC_PROTO_CMD_SYNC, 0);

	request.a0 = strtol(argv[3], &ptr, 16);

	if (argc > 4) {
		request.a1 = strtol(argv[4], &ptr, 16);
	}

	if (argc > 5) {
		request.a2 = strtol(argv[5], &ptr, 16);
	}

	if (argc > 6) {
		request.a3 = strtol(argv[6], &ptr, 16);
	}

	if (argc > 7) {
		request.a4 = strtol(argv[7], &ptr, 16);
	}

	if (argc > 8) {
		request.a5 = strtol(argv[8], &ptr, 16);
	}

	if (argc > 9) {
		request.a6 = strtol(argv[9], &ptr, 16);
	}

	if (argc > 10) {
		request.a7 = strtol(argv[10], &ptr, 16);
	}

	k_sem_init(&(priv->semaphore), 0, 1);
	priv->sh = sh;

	request.resp_data_addr = 0;
	request.resp_data_size = 0;
	request.priv_data = priv;

	trans_id = sip_svc_send(ctrl, (uint32_t)c_token, (uint8_t *)&request,
				sizeof(struct sip_svc_request),
				cmd_send_callback);

	if (trans_id < 0) {
		shell_error(sh, "%s: send fail: client token %08x",
			    ctrl->method, (uint32_t)c_token);
		err = trans_id;
	} else {
		/*wait for callback*/
		k_sem_take(&(priv->semaphore), K_FOREVER);

		shell_print(sh, "%s: send success: client token %08x, trans_id %d",
			    ctrl->method, (uint32_t)c_token, trans_id);
		err = 0;
	}
	k_free(priv);
	return err;
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	struct sip_svc_controller *ctrl;
	int err;
	uint32_t i;
	static const char * const state_str_list[] = {"INVALID",
							"IDLE",
							"OPEN",
							"ABORT"};
	const char *state_str = "UNKNOWN";

	err = parse_common_args(sh, argv, &ctrl);
	if (err < 0) {
		return err;
	}

	shell_print(sh, "---------------------------------------\n");
	shell_print(sh, "sip_svc service information\n");
	shell_print(sh, "---------------------------------------\n");

	if (ctrl->active_client_index == SIP_SVC_ID_INVALID)
		shell_print(sh, "opened client          N/A\n");
	else
		shell_print(sh, "opened client          %08x\n",
		       ctrl->clients[ctrl->active_client_index].token);

	shell_print(sh, "active job cnt         %d\n", ctrl->active_job_cnt);
	shell_print(sh, "active async job cnt   %d\n", ctrl->active_async_job_cnt);

	shell_print(sh, "---------------------------------------\n");
	shell_print(sh, "Client Token\tState\tTrans Cnt\n");
	shell_print(sh, "---------------------------------------\n");
	for (i = 0; i < CONFIG_ARM_SIP_SVC_MAX_CLIENT_COUNT; i++) {
		if (ctrl->clients[i].id != SIP_SVC_ID_INVALID) {
			if (ctrl->clients[i].state <= SIP_SVC_CLIENT_ST_ABORT)
				state_str =
					state_str_list[ctrl->clients[i].state];

			shell_print(sh, "%08x    \t%-10s\t%-9d\n",
				ctrl->clients[i].token,
				state_str,
				ctrl->clients[i].active_trans_cnt);
		}
	}
	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_sip_svc,
	SHELL_CMD_ARG(reg, NULL, "<method>",
		      cmd_reg, 2, 0),
	SHELL_CMD_ARG(unreg, NULL, "<method> <token>",
		      cmd_unreg, 3, 0),
	SHELL_CMD_ARG(open, NULL, "<method> <token> <[timeout_sec]>",
		      cmd_open, 3, 1),
	SHELL_CMD_ARG(close, NULL, "<method> <token>",
		      cmd_close, 3, 0),
	SHELL_CMD_ARG(send, NULL, "<method> <token> <a0> [<a1> <a2> ... <a7>]",
		      cmd_send, 4, 7),
	SHELL_CMD_ARG(info, NULL, "<method>",
		      cmd_info, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(sip_svc, &sub_sip_svc, "Arm SiP services commands",
		   NULL);
