
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <math.h>
#include <nxt_router.h>
#include <nxt_http.h>
#include <nxt_upstream.h>

struct nxt_upstream_round_robin_server_s
{
    nxt_sockaddr_t *sockaddr;

    int32_t current_weight;
    int32_t effective_weight;
    int32_t weight;

    nxt_str_t health;
    nxt_bool_t *health_status;

    uint8_t protocol;
};

struct arg_struct
{
    nxt_upstream_round_robin_t *urr;
    nxt_task_t *task;
};

struct nxt_upstream_round_robin_s
{
    uint32_t items;
    nxt_upstream_round_robin_server_t server[0];
    pthread_t *health_thread;
};

static nxt_upstream_t *nxt_upstream_round_robin_joint_create(
    nxt_router_temp_conf_t *tmcf, nxt_upstream_t *upstream);
static void nxt_upstream_round_robin_server_get(nxt_task_t *task,
                                                nxt_upstream_server_t *us);
// static void nxt_upstream_health_handler(nxt_upstream_round_robin_t *urr);
static void *nxt_upstream_health_handler(void *arg);

static const nxt_upstream_server_proto_t nxt_upstream_round_robin_proto = {
    .joint_create = nxt_upstream_round_robin_joint_create,
    .get = nxt_upstream_round_robin_server_get,
};

nxt_int_t
nxt_upstream_round_robin_create(nxt_task_t *task, nxt_router_temp_conf_t *tmcf,
                                nxt_conf_value_t *upstream_conf, nxt_upstream_t *upstream)
{
    double total, k, w;
    int err;
    size_t size;
    uint32_t i, n, next, wt;
    nxt_mp_t *mp;
    nxt_str_t name, hh;
    nxt_sockaddr_t *sa;
    nxt_conf_value_t *servers_conf, *srvcf, *wtcf, *hhcf;
    nxt_upstream_round_robin_t *urr;
    pthread_t health_thread;
    struct arg_struct *args = malloc(sizeof(struct arg_struct));

    static nxt_str_t servers = nxt_string("servers");
    static nxt_str_t weight = nxt_string("weight");
    static nxt_str_t health = nxt_string("health");

    mp = tmcf->router_conf->mem_pool;

    servers_conf = nxt_conf_get_object_member(upstream_conf, &servers, NULL);
    n = nxt_conf_object_members_count(servers_conf); // Servers

    total = 0.0;
    next = 0;

    for (i = 0; i < n; i++)
    {
        srvcf = nxt_conf_next_object_member(servers_conf, &name, &next); // IP
        wtcf = nxt_conf_get_object_member(srvcf, &weight, NULL);         // Weight
        // hhcf = nxt_conf_get_object_member(srvcf, &health, NULL);         // Health
        w = (wtcf != NULL) ? nxt_conf_get_number(wtcf) : 1;
        total += w;
    }

    /*
     * This prevents overflow of int32_t
     * in nxt_upstream_round_robin_server_get().
     */
    k = (total == 0) ? 0 : (NXT_INT32_T_MAX / 2) / total;

    if (isinf(k))
    {
        k = 1;
    }

    size = sizeof(nxt_upstream_round_robin_t) + n * sizeof(nxt_upstream_round_robin_server_t);

    urr = nxt_mp_zalloc(mp, size);
    if (nxt_slow_path(urr == NULL))
    {
        return NXT_ERROR;
    }

    urr->items = n;
    next = 0;

    for (i = 0; i < n; i++)
    {
        srvcf = nxt_conf_next_object_member(servers_conf, &name, &next); // IP

        sa = nxt_sockaddr_parse(mp, &name);
        if (nxt_slow_path(sa == NULL))
        {
            return NXT_ERROR;
        }

        sa->type = SOCK_STREAM;

        urr->server[i].sockaddr = sa;
        urr->server[i].protocol = NXT_HTTP_PROTO_H1;

        wtcf = nxt_conf_get_object_member(srvcf, &weight, NULL); // WEIGHT
        hhcf = nxt_conf_get_object_member(srvcf, &health, NULL); // HEALTH
        nxt_conf_get_string(hhcf, &hh);
        w = (wtcf != NULL) ? k * nxt_conf_get_number(wtcf) : k;
        wt = (w > 1 || w == 0) ? round(w) : 1;

        urr->server[i].weight = wt;
        urr->server[i].effective_weight = wt;
        urr->server[i].health = hh;
        *(urr->server[i].health_status) = 1;
    }

    args->urr = urr;
    args->task = task;

    nxt_log(task, NXT_LOG_NOTICE, "CATA LOG 1");
    err = pthread_create(&health_thread, NULL, nxt_upstream_health_handler, args);
    nxt_log(task, NXT_LOG_NOTICE, "CATA LOG 2");
    if (err != 0)
    {
        nxt_log(task, NXT_LOG_NOTICE, "CATA LOG 3");
        return NXT_ERROR;
    }
    // urr->health_thread = &health_thread;
    upstream->proto = &nxt_upstream_round_robin_proto;
    upstream->type.round_robin = urr;

    return NXT_OK;
}

static void *nxt_upstream_health_handler(void *arg)
{
    int i, n;
    struct arg_struct *args = (struct arg_struct *)arg;
    nxt_task_t *task = args->task;
    nxt_upstream_round_robin_t *urr = args->urr;
    // struct nxt_http_request_s *r = malloc(sizeof(struct nxt_http_request_s));
    // struct nxt_buf_s *out = malloc(sizeof(struct nxt_buf_s));
    n = urr->items;
    while (1)
    {
        nxt_log(task, NXT_LOG_NOTICE, "CATA LOG REPET");
        for (i = 0; i < n; i++)
        {
            nxt_log(task, NXT_LOG_NOTICE, "CURRENT HEALTH %uz", *(urr->server[i].health_status));
            if (*(urr->server[i].health_status) == 1)
            {
                *(urr->server[i].health_status) = 0;
            }
            else
            {
                *(urr->server[i].health_status) = 1;
            }
            // nxt_http_request_send(task, r, out);
            // }
        }
        sleep(30);
    }
    return NULL;
}

//     nxt_thread_link_t *link;
//     // nxt_str_t hh;
//     uint32_t i, n;
//     n = urr->server;
//     while (1)
//     {
//         for (i = 0; i < n; i++)
//         {
//             // hh = urr->server[i].health;
//             urr->server[i].health_status = 0;
//         }
//         // nxt_debug(task, "upstream health handler: \"%V\"", &urr->items);
//         nxt_nanosleep(30000000000);
//     }
// }

static nxt_upstream_t *
nxt_upstream_round_robin_joint_create(nxt_router_temp_conf_t *tmcf,
                                      nxt_upstream_t *upstream)
{
    size_t size;
    uint32_t i, n;
    nxt_mp_t *mp;
    nxt_upstream_t *u;
    nxt_upstream_round_robin_t *urr, *urrcf;

    mp = tmcf->router_conf->mem_pool;

    u = nxt_mp_alloc(mp, sizeof(nxt_upstream_t));
    if (nxt_slow_path(u == NULL))
    {
        return NULL;
    }

    *u = *upstream;

    urrcf = upstream->type.round_robin;

    size = sizeof(nxt_upstream_round_robin_t) + urrcf->items * sizeof(nxt_upstream_round_robin_server_t);

    urr = nxt_mp_alloc(mp, size);
    if (nxt_slow_path(urr == NULL))
    {
        return NULL;
    }

    u->type.round_robin = urr;

    n = urrcf->items;
    urr->items = n;

    for (i = 0; i < n; i++)
    {
        urr->server[i] = urrcf->server[i];
    }

    return u;
}

static void
nxt_upstream_round_robin_server_get(nxt_task_t *task, nxt_upstream_server_t *us)
{
    int32_t total;
    uint32_t i, n;
    nxt_upstream_round_robin_t *round_robin;
    nxt_upstream_round_robin_server_t *s, *best;

    best = NULL;
    total = 0;

    round_robin = us->upstream->type.round_robin;

    s = round_robin->server;
    n = round_robin->items;

    nxt_log(task, NXT_LOG_NOTICE, "SERVERS %uz",
            n);

    for (i = 0; i < n; i++)
    {
        nxt_log(task, NXT_LOG_NOTICE, "HEALTH %uz", *(s[i].health_status));
        if (*(s[i].health_status) == 0)
        {
            continue;
        }
        s[i].current_weight += s[i].effective_weight;
        total += s[i].effective_weight;

        if (s[i].effective_weight < s[i].weight)
        {
            s[i].effective_weight++;
        }

        if (best == NULL || s[i].current_weight > best->current_weight)
        {
            best = &s[i];
        }
    }

    if (best == NULL || total == 0)
    {
        us->state->error(task, us);
        return;
    }

    best->current_weight -= total;
    us->sockaddr = best->sockaddr;
    us->protocol = best->protocol;
    us->server.round_robin = best;

    us->state->ready(task, us);
}
