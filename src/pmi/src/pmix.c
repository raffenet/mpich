/*
 * Copyright (C) by Argonne National Laboratory
 *     See COPYRIGHT in top-level directory
 */

#ifndef DISABLE_PMIX

#include "pmi_config.h"

#include "pmix.h"
#include "pmi_util.h"
#include "pmi_wire.h"
#include "pmi_msg.h"
#include "pmi_common.h"

#include "mpl.h"

#define USE_WIRE_VER  PMIU_WIRE_V2

/* we will call PMIU_cmd_free_buf */
static const bool no_static = false;
static pmix_proc_t PMIx_proc;
static int PMIx_size;
static int appnum;

static bool cached_singinit_inuse;
static char *cached_singinit_key;

static int getPMIFD(void);

pmix_status_t PMIx_Init(pmix_proc_t * proc, pmix_info_t info[], size_t ninfo)
{
    int pmi_errno = PMIX_SUCCESS;

    /* Get the fd for PMI commands; if none, we're a singleton */
    pmi_errno = getPMIFD();
    PMIU_ERR_POP(pmi_errno);

    if (PMI_fd == -1) {
        /* Singleton init: Process not started with mpiexec */
        PMI_initialized = SINGLETON_INIT_BUT_NO_PM;

        return PMIX_ERR_UNREACH;
    }

    struct PMIU_cmd pmicmd;
    PMIU_cmd_init_zero(&pmicmd);

    /* Get the value of PMI_DEBUG from the environment if possible, since
     * we may have set it to help debug the setup process */
    char *p;
    p = getenv("PMI_DEBUG");
    if (p) {
        PMIU_verbose = atoi(p);
    }

    /* get rank from env */
    const char *s_pmiid;
    int pmiid = -1;
    s_pmiid = getenv("PMI_ID");
    if (!s_pmiid) {
        s_pmiid = getenv("PMI_RANK");
    }
    if (s_pmiid) {
        pmiid = atoi(s_pmiid);
    }
    PMIx_proc.rank = pmiid;

    /* do initial PMI init */
    PMIU_msg_set_query_init(&pmicmd, PMIU_WIRE_V1, no_static, 2, 0);

    pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
    PMIU_ERR_POP(pmi_errno);

    int server_version, server_subversion;
    pmi_errno = PMIU_msg_get_response_init(&pmicmd, &server_version, &server_subversion);

    PMIU_cmd_free_buf(&pmicmd);

    /* do full init */
    PMIU_msg_set_query_fullinit(&pmicmd, USE_WIRE_VER, no_static, pmiid);

    pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
    PMIU_ERR_POP(pmi_errno);

    const char *spawner_jobid = NULL;
    int verbose;                /* unused */
    PMIU_msg_get_response_fullinit(&pmicmd, &pmiid, &PMIx_size, &appnum, &spawner_jobid, &verbose);
    PMIU_ERR_POP(pmi_errno);

    PMIU_cmd_free_buf(&pmicmd);

    /* get the kvsname aka the namespace */
    PMIU_msg_set_query(&pmicmd, USE_WIRE_VER, PMIU_CMD_KVSNAME, no_static);

    pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
    PMIU_ERR_POP(pmi_errno);

    const char *jid;
    pmi_errno = PMIU_msg_get_response_kvsname(&pmicmd, &jid);
    PMIU_ERR_POP(pmi_errno);

    MPL_strncpy(PMIx_proc.nspace, jid, PMIX_MAX_NSLEN + 1);
    PMIU_Set_rank_kvsname(PMI_rank, jid);

    if (!PMI_initialized) {
        PMI_initialized = NORMAL_INIT_WITH_PM;
    }

    *proc = PMIx_proc;

    /* TODO: add reference counting? */

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Finalize(const pmix_info_t info[], size_t ninfo)
{
    int pmi_errno = PMIX_SUCCESS;

    struct PMIU_cmd pmicmd;
    PMIU_cmd_init_zero(&pmicmd);

    if (PMI_initialized > SINGLETON_INIT_BUT_NO_PM) {
        PMIU_msg_set_query(&pmicmd, USE_WIRE_VER, PMIU_CMD_FINALIZE, no_static);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);

        shutdown(PMI_fd, SHUT_RDWR);
        close(PMI_fd);
    }

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Abort(int status, const char msg[], pmix_proc_t procs[], size_t nprocs)
{
    PMIU_printf(1, "aborting job:\n%s\n", msg);

    struct PMIU_cmd pmicmd;
    PMIU_msg_set_query_abort(&pmicmd, USE_WIRE_VER, no_static, status, msg);

    /* ignoring return code, because we're exiting anyway */
    PMIU_cmd_send(PMI_fd, &pmicmd);

    PMIU_Exit(status);
    return PMIX_SUCCESS;
}

pmix_status_t PMIx_Put(pmix_scope_t scope, const char key[], pmix_value_t * val)
{
    int pmi_errno = PMIX_SUCCESS;

    /* This is a special hack to support singleton initialization */
    if (PMI_initialized == SINGLETON_INIT_BUT_NO_PM) {
        if (cached_singinit_inuse)
            return PMIX_ERROR;
        cached_singinit_key = MPL_strdup(key);
        /* FIXME: save copy of value */
        cached_singinit_inuse = true;
        return PMIX_SUCCESS;
    }

    struct PMIU_cmd pmicmd;
    PMIU_cmd_init_zero(&pmicmd);

    if (val->type == PMIX_STRING) {
        char *value = MPL_malloc(strlen(val->data.string) + 3, MPL_MEM_OTHER);
        strcpy(value, "3,");
        strcpy(value + 2, val->data.string);
        PMIU_msg_set_query_kvsput(&pmicmd, USE_WIRE_VER, no_static, key, value);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);

        MPL_free(value);
    } else if (val->type == PMIX_BYTE_OBJECT) {
        /* encode the binary value and prepend type */
        char *encoded_value = MPL_malloc(val->data.bo.size * 2 + 4, MPL_MEM_OTHER);
        strcpy(encoded_value, "27,");
        MPL_hex_encode(val->data.bo.size, val->data.bo.bytes, encoded_value + 3);

        PMIU_msg_set_query_kvsput(&pmicmd, USE_WIRE_VER, no_static, key, encoded_value);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);

        MPL_free(encoded_value);
    }

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Commit(void)
{
    return PMIX_SUCCESS;
}

pmix_status_t PMIx_Fence(const pmix_proc_t procs[], size_t nprocs,
                         const pmix_info_t info[], size_t ninfo)
{
    int pmi_errno = PMIX_SUCCESS;

    struct PMIU_cmd pmicmd;
    PMIU_cmd_init_zero(&pmicmd);

    if (PMI_initialized > SINGLETON_INIT_BUT_NO_PM) {
        PMIU_msg_set_query(&pmicmd, USE_WIRE_VER, PMIU_CMD_KVSFENCE, no_static);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);
    }

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Get(const pmix_proc_t * proc, const char key[],
                       const pmix_info_t info[], size_t ninfo, pmix_value_t ** val)
{
    int pmi_errno = PMIX_SUCCESS;

    /* first check for information we have locally */
    if (!strcmp(key, PMIX_JOB_SIZE)) {
        *val = MPL_direct_malloc(sizeof(pmix_value_t));
        (*val)->type = PMIX_UINT32;
        (*val)->data.uint32 = PMIx_size;
        return PMIX_SUCCESS;
    }

    if (!strcmp(key, PMIX_APPNUM)) {
        *val = MPL_direct_malloc(sizeof(pmix_value_t));
        (*val)->type = PMIX_UINT32;
        (*val)->data.uint32 = appnum;
        return PMIX_SUCCESS;
    }

    /* if there is no PMI server, just return not found */
    if (PMI_initialized <= SINGLETON_INIT_BUT_NO_PM) {
        return PMIX_ERR_NOT_FOUND;
    }

    if (!strcmp(key, PMIX_PARENT_ID)) {
        return PMIX_ERR_NOT_FOUND;
    }

    struct PMIU_cmd pmicmd;
    PMIU_cmd_init_zero(&pmicmd);

    /* handle special predefined keys */
    if (!strcmp(key, "PMI_process_mapping") || !strcmp(key, "PMI_hwloc_xmlfile") ||
        !strcmp(key, PMIX_UNIV_SIZE) || !strcmp(key, PMIX_ANL_MAP)) {
        PMIU_msg_set_query_get(&pmicmd, USE_WIRE_VER, no_static, NULL, key);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);

        bool found;
        const char *tmp_val;
        if (pmi_errno == PMIU_SUCCESS) {
            pmi_errno = PMIU_msg_get_response_get(&pmicmd, &tmp_val, &found);
        }

        if (!pmi_errno && found) {
            *val = MPL_direct_malloc(sizeof(pmix_value_t));
            if (!strcmp(key, PMIX_UNIV_SIZE)) {
                (*val)->type = PMIX_UINT32;
                (*val)->data.uint32 = atoi(tmp_val);
            } else {
                (*val)->type = PMIX_STRING;
                (*val)->data.string = MPL_direct_strdup(tmp_val);
            }
        } else {
            pmi_errno = PMIX_ERR_NOT_FOUND;
        }

        PMIU_cmd_free_buf(&pmicmd);
        goto fn_exit;
    }

    const char *nspace = PMIx_proc.nspace;
    int srcid = -1;
    if (proc != NULL) {
        /* user-provided namespace might be the empty string, ignore it */
        if (strlen(proc->nspace) != 0) {
            nspace = proc->nspace;
        }
        srcid = proc->rank;
    }
    PMIU_msg_set_query_kvsget(&pmicmd, USE_WIRE_VER, no_static, nspace, srcid, key);

    pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
    PMIU_ERR_POP(pmi_errno);

    const char *tmp_val;
    bool found;
    pmi_errno = PMIU_msg_get_response_kvsget(&pmicmd, &tmp_val, &found);
    PMIU_ERR_POP(pmi_errno);

    if (found) {
        char *s = MPL_strdup(tmp_val);
        char *type = strtok(s, ",");
        char *raw_value = strtok(NULL, ",");

        *val = MPL_direct_malloc(sizeof(pmix_value_t));
        (*val)->type = atoi(type);

        if ((*val)->type == PMIX_STRING) {
            (*val)->data.string = MPL_direct_strdup(raw_value);
        } else if ((*val)->type == PMIX_BYTE_OBJECT) {
            int n = strlen(raw_value) / 2;
            (*val)->data.bo.bytes = MPL_direct_malloc(n);
            (*val)->data.bo.size = n;
            MPL_hex_decode(n, raw_value, (*val)->data.bo.bytes);
        }

        MPL_free(s);
    } else {
        pmi_errno = PMIX_ERR_NOT_FOUND;
    }

    PMIU_cmd_free_buf(&pmicmd);

  fn_exit:
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Info_load(pmix_info_t * info,
                             const char *key, const void *data, pmix_data_type_t type)
{
    MPL_strncpy(info->key, key, PMIX_MAX_KEYLEN + 1);
    info->value.type = type;
    if (type == PMIX_BOOL) {
        info->value.data.flag = *(bool *) data;
    }

    return PMIX_SUCCESS;
}

pmix_status_t PMIx_Publish(const pmix_info_t info[], size_t ninfo)
{
    int pmi_errno = PMIX_SUCCESS;

    struct PMIU_cmd pmicmd;

    for (int i = 0; i < ninfo; i++) {
        assert(info[i].value.type == PMIX_STRING);
        PMIU_msg_set_query_publish(&pmicmd, USE_WIRE_VER, no_static, info[i].key,
                                   info[i].value.data.string);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);
    }

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Lookup(pmix_pdata_t data[], size_t ndata, const pmix_info_t info[], size_t ninfo)
{
    int pmi_errno = PMIX_SUCCESS;

    struct PMIU_cmd pmicmd;

    for (int i = 0; i < ndata; i++) {
        PMIU_msg_set_query_lookup(&pmicmd, USE_WIRE_VER, no_static, data[i].key);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);

        const char *tmp_port;
        pmi_errno = PMIU_msg_get_response_lookup(&pmicmd, &tmp_port);

        data[i].value.type = PMIX_STRING;
        data[i].value.data.string = MPL_direct_strdup(tmp_port);
    }

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Unpublish(char **keys, const pmix_info_t info[], size_t ninfo)
{
    int pmi_errno = PMIX_SUCCESS;

    struct PMIU_cmd pmicmd;

    for (int i = 0; keys[i]; i++) {
        PMIU_msg_set_query_unpublish(&pmicmd, USE_WIRE_VER, no_static, keys[i]);

        pmi_errno = PMIU_cmd_get_response(PMI_fd, &pmicmd);
        PMIU_ERR_POP(pmi_errno);
    }

  fn_exit:
    PMIU_cmd_free_buf(&pmicmd);
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

pmix_status_t PMIx_Resolve_peers(const char *nodename,
                                 const pmix_nspace_t nspace, pmix_proc_t ** procs, size_t * nprocs)
{
    assert(0);
}

pmix_status_t PMIx_Resolve_nodes(const pmix_nspace_t nspace, char **nodelist)
{
    assert(0);
}

const char *PMIx_Error_string(pmix_status_t status)
{
    return "ERROR";
}

pmix_status_t PMIx_Spawn(const pmix_info_t job_info[], size_t ninfo,
                         const pmix_app_t apps[], size_t napps, pmix_nspace_t nspace)
{
    assert(0);
}

/* stub for connecting to a specified host/port instead of using a
   specified fd inherited from a parent process */
static int PMII_Connect_to_pm(char *hostname, int portnum)
{
    int ret;
    MPL_sockaddr_t addr;
    int fd;
    int optval = 1;
    int q_wait = 1;

    ret = MPL_get_sockaddr(hostname, &addr);
    if (ret) {
        PMIU_printf(1, "Unable to get host entry for %s\n", hostname);
        return -1;
    }

    fd = MPL_socket();
    if (fd < 0) {
        PMIU_printf(1, "Unable to get AF_INET socket\n");
        return -1;
    }

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &optval, sizeof(optval))) {
        perror("Error calling setsockopt:");
    }

    ret = MPL_connect(fd, &addr, portnum);
    /* We wait here for the connection to succeed */
    if (ret) {
        switch (errno) {
            case ECONNREFUSED:
                PMIU_printf(1, "connect failed with connection refused\n");
                /* (close socket, get new socket, try again) */
                if (q_wait)
                    close(fd);
                return -1;

            case EINPROGRESS:  /*  (nonblocking) - select for writing. */
                break;

            case EISCONN:      /*  (already connected) */
                break;

            case ETIMEDOUT:    /* timed out */
                PMIU_printf(1, "connect failed with timeout\n");
                return -1;

            default:
                PMIU_printf(1, "connect failed with errno %d\n", errno);
                return -1;
        }
    }

    return fd;
}

/* Get the FD to use for PMI operations.  If a port is used, rather than
   a pre-established FD (i.e., via pipe), this routine will handle the
   initial handshake.
*/
static int getPMIFD(void)
{
    int pmi_errno = PMIX_SUCCESS;
    char *p;

    /* Set the default */
    PMI_fd = -1;

    p = getenv("PMI_FD");
    if (p) {
        PMI_fd = atoi(p);
        goto fn_exit;
    }

    p = getenv("PMI_PORT");
    if (p) {
        int portnum;
        char hostname[MAXHOSTNAME + 1];
        char *pn, *ph;

        /* Connect to the indicated port (in format hostname:portnumber)
         * and get the fd for the socket */

        /* Split p into host and port */
        pn = p;
        ph = hostname;
        while (*pn && *pn != ':' && (ph - hostname) < MAXHOSTNAME) {
            *ph++ = *pn++;
        }
        *ph = 0;

        PMIU_ERR_CHKANDJUMP1(*pn != ':', pmi_errno, PMIX_ERROR, "**pmix_port %s", p);

        portnum = atoi(pn + 1);
        /* FIXME: Check for valid integer after : */
        /* This routine only gets the fd to use to talk to
         * the process manager. The handshake below is used
         * to setup the initial values */
        PMI_fd = PMII_Connect_to_pm(hostname, portnum);
        PMIU_ERR_CHKANDJUMP2(PMI_fd < 0, pmi_errno, PMIX_ERROR,
                             "**connect_to_pm %s %d", hostname, portnum);
    }

    /* OK to return success for singleton init */

  fn_exit:
    return pmi_errno;
  fn_fail:
    goto fn_exit;
}

#endif /* DISABLE_PMIX */
