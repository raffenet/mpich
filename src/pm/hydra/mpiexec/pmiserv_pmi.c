/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#include "hydra_server.h"
#include "pmiserv_pmi.h"
#include "pmiserv_utils.h"

struct HYD_pmcd_pmi_handle *HYD_pmcd_pmi_handle = { 0 };

struct HYD_proxy *HYD_pmcd_pmi_find_proxy(int fd)
{
    struct HYD_proxy *proxy;

    HASH_FIND_INT(HYD_server_info.proxy_hash, &fd, proxy);

    return proxy;
}

HYD_status HYD_pmcd_pmi_finalize(void)
{
    HYD_status status = HYD_SUCCESS;

    HYDU_FUNC_ENTER();

    HYDU_FUNC_EXIT();
    return status;
}

HYD_status HYD_pmiserv_pmi_reply(int fd, int pid, struct PMIU_cmd * pmi)
{
    struct HYD_pmcd_hdr hdr;
    HYD_pmcd_init_header(&hdr);
    hdr.cmd = CMD_PMI_RESPONSE;
    hdr.u.pmi.pid = pid;
    return HYD_pmcd_pmi_send(fd, pmi, &hdr, HYD_server_info.user_global.debug);
}
