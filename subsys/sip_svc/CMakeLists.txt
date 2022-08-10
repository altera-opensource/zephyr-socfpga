# Copyright (c) 2021, Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

if(CONFIG_ARM_SIP_SVC_ARM64)
  zephyr_library_sources(sip_svc.c)
  zephyr_library_sources(sip_svc_id_mgr.c)

  zephyr_library_sources_ifdef(CONFIG_ARM_SIP_SVC_SHELL sip_svc_shell.c)
  zephyr_library_sources_ifdef(CONFIG_ARM_SIP_SVC_MAILBOX_CLIENT_SHELL sip_svc_agilex_mailbox_shell.c)

  zephyr_library_sources_ifdef(CONFIG_SOC_SERIES_AGILEX sip_svc_intel_socfpga.c)

endif()
