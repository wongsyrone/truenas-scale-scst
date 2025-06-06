/*
 *  Copyright (C) 2002 - 2003 Ardis Technologies <roman@ardistech.com>
 *  Copyright (C) 2007 - 2018 Vladislav Bolkhovitin
 *  Copyright (C) 2007 - 2018 Western Digital Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation, version 2
 *  of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#include <linux/file.h>
#include <linux/ip.h>
#include <net/tcp.h>
#ifdef INSIDE_KERNEL_TREE
#include <scst/iscsit_transport.h>
#else
#include "iscsit_transport.h"
#endif
#include "iscsi_trace_flag.h"
#include "iscsi.h"
#include "digest.h"

#undef DEFAULT_SYMBOL_NAMESPACE
#define DEFAULT_SYMBOL_NAMESPACE	SCST_NAMESPACE

#if defined(CONFIG_LOCKDEP) && !defined(CONFIG_SCST_PROC)
static struct lock_class_key scst_conn_key;
static struct lockdep_map scst_conn_dep_map =
	STATIC_LOCKDEP_MAP_INIT("iscsi_conn_kref", &scst_conn_key);
#endif

static ssize_t print_conn_state(char *buf, size_t size, struct iscsi_conn *conn)
{
	ssize_t ret = 0;

	if (conn->closing) {
		ret += scnprintf(buf, size, "closing");
		goto out;
	}

	switch (conn->rd_state) {
	case ISCSI_CONN_RD_STATE_PROCESSING:
		ret += scnprintf(buf + ret, size - ret, "read_processing ");
		break;
	case ISCSI_CONN_RD_STATE_IN_LIST:
		ret += scnprintf(buf + ret, size - ret, "in_read_list ");
		break;
	}

	switch (conn->wr_state) {
	case ISCSI_CONN_WR_STATE_PROCESSING:
		ret += scnprintf(buf + ret, size - ret, "write_processing ");
		break;
	case ISCSI_CONN_WR_STATE_IN_LIST:
		ret += scnprintf(buf + ret, size - ret, "in_write_list ");
		break;
	case ISCSI_CONN_WR_STATE_SPACE_WAIT:
		ret += scnprintf(buf + ret, size - ret, "space_waiting ");
		break;
	}

	if (test_bit(ISCSI_CONN_REINSTATING, &conn->conn_aflags))
		ret += scnprintf(buf + ret, size - ret, "reinstating ");
	else if (ret == 0)
		ret += scnprintf(buf + ret, size - ret, "established idle ");

out:
	return ret;
}

static void iscsi_conn_release(struct kobject *kobj)
{
	struct iscsi_conn *conn;

	TRACE_ENTRY();

	conn = container_of(kobj, struct iscsi_conn, conn_kobj);
	if (conn->conn_kobj_release_cmpl)
		complete_all(conn->conn_kobj_release_cmpl);

	TRACE_EXIT();
}

struct kobj_type iscsi_conn_ktype = {
	.release = iscsi_conn_release,
};

static ssize_t iscsi_get_initiator_ip(struct iscsi_conn *conn, char *buf, int size)
{
	return conn->transport->iscsit_get_initiator_ip(conn, buf, size);
}

static ssize_t iscsi_conn_ip_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct iscsi_conn *conn;
	ssize_t ret;

	TRACE_ENTRY();

	conn = container_of(kobj, struct iscsi_conn, conn_kobj);

	ret = iscsi_get_initiator_ip(conn, buf, SCST_SYSFS_BLOCK_SIZE);

	TRACE_EXIT_RES(ret);
	return ret;
}

static struct kobj_attribute iscsi_conn_ip_attr =
	__ATTR(ip, 0444, iscsi_conn_ip_show, NULL);

static ssize_t iscsi_get_target_ip(struct iscsi_conn *conn, char *buf, int size)
{
	struct sock *sk;
	ssize_t ret;

	TRACE_ENTRY();

	if (!conn->sock)
		return -ENOENT;

	sk = conn->sock->sk;
	switch (sk->sk_family) {
	case AF_INET:
		ret = scnprintf(buf, size, "%pI4", &inet_sk(sk)->inet_saddr);
		break;
#ifdef CONFIG_IPV6
	case AF_INET6:
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
		ret = scnprintf(buf, size, "[%pI6]", &inet6_sk(sk)->saddr);
#else
		ret = scnprintf(buf, size, "[%pI6]", &sk->sk_v6_rcv_saddr);
#endif
#endif
		break;
	default:
		ret = scnprintf(buf, size, "Unknown family %d", sk->sk_family);
		break;
	}

	TRACE_EXIT_RES(ret);
	return ret;
}

static ssize_t iscsi_conn_target_ip_show(struct kobject *kobj, struct kobj_attribute *attr,
					 char *buf)
{
	struct iscsi_conn *conn;
	ssize_t ret;

	TRACE_ENTRY();

	conn = container_of(kobj, struct iscsi_conn, conn_kobj);

	ret = iscsi_get_target_ip(conn, buf, SCST_SYSFS_BLOCK_SIZE);

	TRACE_EXIT_RES(ret);
	return ret;
}

static struct kobj_attribute iscsi_conn_target_ip_attr =
	__ATTR(target_ip, 0444, iscsi_conn_target_ip_show, NULL);

static ssize_t iscsi_conn_transport_show(struct kobject *kobj, struct kobj_attribute *attr,
					 char *buf)
{
	struct iscsi_conn *conn;
	ssize_t ret;

	TRACE_ENTRY();

	conn = container_of(kobj, struct iscsi_conn, conn_kobj);

	ret = sysfs_emit(buf, "%s\n", conn->transport->name);

	TRACE_EXIT_RES(ret);
	return ret;
}

static struct kobj_attribute iscsi_conn_transport_attr =
	__ATTR(transport, 0444, iscsi_conn_transport_show, NULL);

static ssize_t iscsi_conn_cid_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct iscsi_conn *conn;
	ssize_t ret;

	TRACE_ENTRY();

	conn = container_of(kobj, struct iscsi_conn, conn_kobj);

	ret = sysfs_emit(buf, "%u", conn->cid);

	TRACE_EXIT_RES(ret);
	return ret;
}

static struct kobj_attribute iscsi_conn_cid_attr =
	__ATTR(cid, 0444, iscsi_conn_cid_show, NULL);

static ssize_t iscsi_conn_state_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct iscsi_conn *conn;
	ssize_t ret;

	TRACE_ENTRY();

	conn = container_of(kobj, struct iscsi_conn, conn_kobj);

	ret = print_conn_state(buf, SCST_SYSFS_BLOCK_SIZE, conn);

	TRACE_EXIT_RES(ret);
	return ret;
}

static struct kobj_attribute iscsi_conn_state_attr =
	__ATTR(state, 0444, iscsi_conn_state_show, NULL);

static void conn_sysfs_del(struct iscsi_conn *conn)
{
	DECLARE_COMPLETION_ONSTACK(c);

	TRACE_ENTRY();

	conn->conn_kobj_release_cmpl = &c;

	kobject_del(&conn->conn_kobj);

	SCST_KOBJECT_PUT_AND_WAIT(&conn->conn_kobj, "conn",
				  conn->conn_kobj_release_cmpl,
				  &scst_conn_dep_map);

	TRACE_EXIT();
}

int conn_sysfs_add(struct iscsi_conn *conn)
{
	int res;
	struct iscsi_session *session = conn->session;
	struct iscsi_conn *c;
	int n = 1;
	char addr[64];

	TRACE_ENTRY();

	lockdep_assert_held(&conn->target->target_mutex);

	iscsi_get_initiator_ip(conn, addr, sizeof(addr));

restart:
	list_for_each_entry(c, &session->conn_list, conn_list_entry) {
		if (strcmp(addr, kobject_name(&c->conn_kobj)) == 0) {
			char c_addr[64];

			iscsi_get_initiator_ip(conn, c_addr, sizeof(c_addr));

			TRACE_DBG("Duplicated conn from the same initiator %s found",
				  c_addr);

			snprintf(addr, sizeof(addr), "%s_%d", c_addr, n);
			n++;
			goto restart;
		}
	}

	res = kobject_init_and_add(&conn->conn_kobj, &iscsi_conn_ktype,
				   scst_sysfs_get_sess_kobj(session->scst_sess), "%s", addr);
	if (res != 0) {
		PRINT_ERROR("Unable create sysfs entries for conn %s",
			    addr);
		goto out;
	}

	TRACE_DBG("conn %p, conn_kobj %p", conn, &conn->conn_kobj);

	res = sysfs_create_file(&conn->conn_kobj, &iscsi_conn_state_attr.attr);
	if (res != 0) {
		PRINT_ERROR("Unable create sysfs attribute %s for conn %s",
			    iscsi_conn_state_attr.attr.name, addr);
		goto out_err;
	}

	res = sysfs_create_file(&conn->conn_kobj, &iscsi_conn_cid_attr.attr);
	if (res != 0) {
		PRINT_ERROR("Unable create sysfs attribute %s for conn %s",
			    iscsi_conn_cid_attr.attr.name, addr);
		goto out_err;
	}

	res = sysfs_create_file(&conn->conn_kobj, &iscsi_conn_ip_attr.attr);
	if (res != 0) {
		PRINT_ERROR("Unable create sysfs attribute %s for conn %s",
			    iscsi_conn_ip_attr.attr.name, addr);
		goto out_err;
	}

	res = sysfs_create_file(&conn->conn_kobj, &iscsi_conn_target_ip_attr.attr);
	if (res != 0) {
		PRINT_ERROR("Unable create sysfs attribute %s for conn %s",
			    iscsi_conn_target_ip_attr.attr.name, addr);
		goto out_err;
	}

	res = sysfs_create_file(&conn->conn_kobj, &iscsi_conn_transport_attr.attr);
	if (res != 0) {
		PRINT_ERROR("Unable create sysfs attribute %s for conn %s",
			    iscsi_conn_transport_attr.attr.name, addr);
		goto out_err;
	}

out:
	TRACE_EXIT_RES(res);
	return res;

out_err:
	conn_sysfs_del(conn);
	goto out;
}
EXPORT_SYMBOL(conn_sysfs_add);

/* target_mutex supposed to be locked */
struct iscsi_conn *conn_lookup(struct iscsi_session *session, u16 cid)
{
	struct iscsi_conn *conn;

	lockdep_assert_held(&session->target->target_mutex);

	/*
	 * We need to find the latest conn to correctly handle
	 * multi-reinstatements
	 */
	list_for_each_entry_reverse(conn, &session->conn_list, conn_list_entry) {
		if (conn->cid == cid && !conn->closing)
			return conn;
	}
	return NULL;
}

void iscsi_make_conn_rd_active(struct iscsi_conn *conn)
{
	struct iscsi_thread_pool *p = conn->conn_thr_pool;

	TRACE_ENTRY();

	spin_lock_bh(&p->rd_lock);

	TRACE_DBG("conn %p, rd_state %x, rd_data_ready %d",
		  conn, conn->rd_state, conn->rd_data_ready);

	/*
	 * Let's start processing ASAP not waiting for all the being waited
	 * data be received, even if we need several wakup iteration to receive
	 * them all, because starting ASAP, i.e. in parallel, is better for
	 * performance, especially on multi-CPU/core systems.
	 */

	conn->rd_data_ready = 1;

	if (conn->rd_state == ISCSI_CONN_RD_STATE_IDLE) {
		list_add_tail(&conn->rd_list_entry, &p->rd_list);
		conn->rd_state = ISCSI_CONN_RD_STATE_IN_LIST;
		wake_up(&p->rd_waitQ);
	}

	spin_unlock_bh(&p->rd_lock);

	TRACE_EXIT();
}

void iscsi_make_conn_wr_active(struct iscsi_conn *conn)
{
	struct iscsi_thread_pool *p = conn->conn_thr_pool;

	TRACE_ENTRY();

	spin_lock_bh(&p->wr_lock);

	TRACE_DBG("conn %p, wr_state %x, wr_space_ready %d", conn,
		  conn->wr_state, conn->wr_space_ready);

	/*
	 * Let's start sending waiting to be sent data ASAP, even if there's
	 * still not all the needed buffers ready and we need several wakup
	 * iteration to send them all, because starting ASAP, i.e. in parallel,
	 * is better for performance, especially on multi-CPU/core systems.
	 */

	if (conn->wr_state == ISCSI_CONN_WR_STATE_IDLE) {
		list_add_tail(&conn->wr_list_entry, &p->wr_list);
		conn->wr_state = ISCSI_CONN_WR_STATE_IN_LIST;
		wake_up(&p->wr_waitQ);
	}

	spin_unlock_bh(&p->wr_lock);

	TRACE_EXIT();
}

void iscsi_tcp_mark_conn_closed(struct iscsi_conn *conn, int flags)
{
	spin_lock_bh(&conn->conn_thr_pool->rd_lock);
	conn->closing = 1;
	if (flags & ISCSI_CONN_ACTIVE_CLOSE)
		conn->active_close = 1;
	if (flags & ISCSI_CONN_DELETING)
		conn->deleting = 1;
	spin_unlock_bh(&conn->conn_thr_pool->rd_lock);

	iscsi_make_conn_rd_active(conn);
}

void __mark_conn_closed(struct iscsi_conn *conn, int flags)
{
	conn->transport->iscsit_mark_conn_closed(conn, flags);
}

void mark_conn_closed(struct iscsi_conn *conn)
{
	__mark_conn_closed(conn, ISCSI_CONN_ACTIVE_CLOSE);
}
EXPORT_SYMBOL(mark_conn_closed);

static void __iscsi_state_change(struct sock *sk)
{
	struct iscsi_conn *conn = sk->sk_user_data;

	TRACE_ENTRY();

	if (unlikely(sk->sk_state != TCP_ESTABLISHED)) {
		if (!conn->closing) {
			PRINT_ERROR("Connection %p with initiator %s unexpectedly closed!",
				    conn, conn->session->initiator_name);
			TRACE_MGMT_DBG("conn %p, sk state %d",
				       conn, sk->sk_state);
			__mark_conn_closed(conn, 0);
		}
	} else {
		iscsi_make_conn_rd_active(conn);
	}

	TRACE_EXIT();
}

static void iscsi_state_change(struct sock *sk)
{
	struct iscsi_conn *conn = sk->sk_user_data;

	__iscsi_state_change(sk);
	conn->old_state_change(sk);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
static void iscsi_data_ready(struct sock *sk)
#else
static void iscsi_data_ready(struct sock *sk, int len)
#endif
{
	struct iscsi_conn *conn = sk->sk_user_data;

	TRACE_ENTRY();

	iscsi_make_conn_rd_active(conn);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))
	conn->old_data_ready(sk);
#else
	conn->old_data_ready(sk, len);
#endif

	TRACE_EXIT();
}

void __iscsi_write_space_ready(struct iscsi_conn *conn)
{
	struct iscsi_thread_pool *p = conn->conn_thr_pool;

	TRACE_ENTRY();

	spin_lock_bh(&p->wr_lock);
	conn->wr_space_ready = 1;
	if (conn->wr_state == ISCSI_CONN_WR_STATE_SPACE_WAIT) {
		TRACE_DBG("wr space ready (conn %p)", conn);
		list_add_tail(&conn->wr_list_entry, &p->wr_list);
		conn->wr_state = ISCSI_CONN_WR_STATE_IN_LIST;
		wake_up(&p->wr_waitQ);
	}
	spin_unlock_bh(&p->wr_lock);

	TRACE_EXIT();
}

static void iscsi_write_space_ready(struct sock *sk)
{
	struct iscsi_conn *conn = sk->sk_user_data;

	TRACE_ENTRY();

	TRACE_DBG("Write space ready for conn %p", conn);

	__iscsi_write_space_ready(conn);

	conn->old_write_space(sk);

	TRACE_EXIT();
}

static void conn_rsp_timer_fn(struct timer_list *timer)
{
	struct iscsi_conn *conn = container_of(timer, typeof(*conn), rsp_timer);
	struct iscsi_cmnd *cmnd;
	unsigned long j = jiffies;

	TRACE_ENTRY();

	TRACE_DBG("Timer (conn %p)", conn);

	spin_lock_bh(&conn->write_list_lock);

	if (!list_empty(&conn->write_timeout_list)) {
		unsigned long timeout_time;

		cmnd = list_first_entry(&conn->write_timeout_list, struct iscsi_cmnd,
					write_timeout_list_entry);

		timeout_time = iscsi_get_timeout_time(cmnd) + ISCSI_ADD_SCHED_TIME;

		if (unlikely(time_after_eq(j, iscsi_get_timeout_time(cmnd)))) {
			if (!conn->closing) {
				PRINT_ERROR("Timeout %ld sec sending data/waiting for reply to/from initiator %s (SID %llx), closing connection %p",
					    iscsi_get_timeout(cmnd) / HZ,
					    conn->session->initiator_name,
					    (unsigned long long)conn->session->sid,
					    conn);
				/*
				 * We must call mark_conn_closed() outside of
				 * write_list_lock or we will have a circular
				 * locking dependency with rd_lock.
				 */
				spin_unlock_bh(&conn->write_list_lock);
				mark_conn_closed(conn);
				goto out;
			}
		} else if (!timer_pending(&conn->rsp_timer) ||
			    time_after(conn->rsp_timer.expires, timeout_time)) {
			TRACE_DBG("Restarting timer on %ld (conn %p)",
				  timeout_time, conn);
			/*
			 * Timer might have been restarted while we were
			 * entering here.
			 *
			 * Since we have not empty write_timeout_list, we are
			 * safe to restart the timer, because we not race with
			 * timer_delete_sync() in conn_free().
			 */
			mod_timer(&conn->rsp_timer, timeout_time);
		}
	}

	spin_unlock_bh(&conn->write_list_lock);

	if (unlikely(conn->conn_tm_active)) {
		TRACE_MGMT_DBG("TM active: making conn %p RD active", conn);
		iscsi_make_conn_rd_active(conn);
	}

out:
	TRACE_EXIT();
}

static void conn_nop_in_delayed_work_fn(struct work_struct *work)
{
	struct iscsi_conn *conn = container_of(work, struct iscsi_conn,
					       nop_in_delayed_work.work);
	unsigned long next_timeout = 0;

	TRACE_ENTRY();

	if (time_after_eq(jiffies, conn->last_rcv_time +
				conn->nop_in_interval)) {
		if (list_empty(&conn->nop_req_list))
			iscsi_send_nop_in(conn);
		next_timeout = conn->nop_in_interval;
	}

	if (conn->nop_in_interval > 0 && !test_bit(ISCSI_CONN_SHUTTINGDOWN, &conn->conn_aflags)) {
		if (next_timeout == 0)
			next_timeout = conn->nop_in_interval -
						(jiffies - conn->last_rcv_time);
		TRACE_DBG("Reschedule Nop-In work for conn %p in %lu", conn,
			  next_timeout + ISCSI_ADD_SCHED_TIME);
		schedule_delayed_work(&conn->nop_in_delayed_work,
				      next_timeout + ISCSI_ADD_SCHED_TIME);
	}

	TRACE_EXIT();
}

/* Must be called from rd thread only */
void iscsi_check_tm_data_wait_timeouts(struct iscsi_conn *conn, bool force)
{
	struct iscsi_cmnd *cmnd;
	unsigned long j = jiffies;
	bool aborted_cmds_pending;
	unsigned long timeout_time = j + ISCSI_TM_DATA_WAIT_TIMEOUT +
					ISCSI_ADD_SCHED_TIME;

	TRACE_ENTRY();

	TRACE_DBG_FLAG(TRACE_MGMT_DEBUG,
		       "conn %p, read_cmnd %p, read_state %d, j %ld (TIMEOUT %d, force %d)",
		       conn, conn->read_cmnd, conn->read_state, j,
		       ISCSI_TM_DATA_WAIT_TIMEOUT + ISCSI_ADD_SCHED_TIME, force);

	iscsi_extracheck_is_rd_thread(conn);

again:
	spin_lock_bh(&conn->conn_thr_pool->rd_lock);
	spin_lock(&conn->write_list_lock);

	aborted_cmds_pending = false;
	list_for_each_entry(cmnd, &conn->write_timeout_list, write_timeout_list_entry) {
		/*
		 * This should not happen, because DATA OUT commands can't get
		 * into write_timeout_list.
		 */
		sBUG_ON(cmnd->cmd_req);

		if (test_bit(ISCSI_CMD_ABORTED, &cmnd->prelim_compl_flags)) {
			TRACE_MGMT_DBG("Checking aborted cmnd %p (scst_state %d, on_write_timeout_list %d, write_start %ld, r2t_len_to_receive %d)",
				       cmnd,
				       cmnd->scst_state, cmnd->on_write_timeout_list,
				       cmnd->write_start, cmnd->r2t_len_to_receive);
			if (cmnd == conn->read_cmnd || cmnd->data_out_in_data_receiving) {
				sBUG_ON((cmnd == conn->read_cmnd) && force);
				/*
				 * We can't abort command waiting for data from
				 * the net, because otherwise we are risking to
				 * get out of sync with the sender, so we have
				 * to wait until the timeout timer gets into the
				 * action and close this connection.
				 */
				TRACE_MGMT_DBG("Aborted cmnd %p is %s, keep waiting",
					       cmnd,
					       cmnd == conn->read_cmnd ? "RX cmnd" :
					       "waiting for DATA OUT data");
				goto cont;
			}
			if (cmnd->r2t_len_to_receive != 0 &&
			    (time_after_eq(j, cmnd->write_start + ISCSI_TM_DATA_WAIT_TIMEOUT) ||
			     force)) {
				spin_unlock(&conn->write_list_lock);
				spin_unlock_bh(&conn->conn_thr_pool->rd_lock);
				iscsi_fail_data_waiting_cmnd(cmnd);
				goto again;
			}
cont:
			aborted_cmds_pending = true;
		}
	}

	if (aborted_cmds_pending) {
		if (!force &&
		    (!timer_pending(&conn->rsp_timer) ||
		     time_after(conn->rsp_timer.expires, timeout_time))) {
			TRACE_MGMT_DBG("Mod timer on %ld (conn %p)",
				       timeout_time, conn);
			mod_timer(&conn->rsp_timer, timeout_time);
		}
	} else {
		TRACE_MGMT_DBG("Clearing conn_tm_active for conn %p", conn);
		conn->conn_tm_active = 0;
	}

	spin_unlock(&conn->write_list_lock);
	spin_unlock_bh(&conn->conn_thr_pool->rd_lock);

	TRACE_EXIT();
}

/* target_mutex supposed to be locked */
void conn_reinst_finished(struct iscsi_conn *conn)
{
	struct iscsi_cmnd *cmnd, *t;

	TRACE_ENTRY();

	clear_bit(ISCSI_CONN_REINSTATING, &conn->conn_aflags);

	list_for_each_entry_safe(cmnd, t, &conn->reinst_pending_cmd_list,
				 reinst_pending_cmd_list_entry) {
		TRACE_MGMT_DBG("Restarting reinst pending cmnd %p",
			       cmnd);

		list_del(&cmnd->reinst_pending_cmd_list_entry);

		/* Restore the state for preliminary completion/cmnd_done() */
		cmnd->scst_state = ISCSI_CMD_STATE_AFTER_PREPROC;

		iscsi_restart_cmnd(cmnd);
	}

	TRACE_EXIT();
}

int conn_activate(struct iscsi_conn *conn)
{
	TRACE_MGMT_DBG("Enabling conn %p", conn);

	/* Catch double bind */
	sBUG_ON(conn->sock->sk->sk_state_change == iscsi_state_change);

	write_lock_bh(&conn->sock->sk->sk_callback_lock);

	conn->old_state_change = conn->sock->sk->sk_state_change;
	conn->sock->sk->sk_state_change = iscsi_state_change;

	conn->old_data_ready = conn->sock->sk->sk_data_ready;
	conn->sock->sk->sk_data_ready = iscsi_data_ready;

	conn->old_write_space = conn->sock->sk->sk_write_space;
	conn->sock->sk->sk_write_space = iscsi_write_space_ready;

	write_unlock_bh(&conn->sock->sk->sk_callback_lock);

	/*
	 * Check, if conn was closed while we were initializing it.
	 * This function will make conn rd_active, if necessary.
	 */
	__iscsi_state_change(conn->sock->sk);

	return 0;
}

static int conn_setup_sock(struct iscsi_conn *conn)
{
	int opt = 1;
	mm_segment_t oldfs;
	struct iscsi_session *session = conn->session;

	TRACE_DBG("%llx", (unsigned long long)session->sid);

	conn->sock = SOCKET_I(file_inode(conn->file));

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
	if (!conn->sock->ops->sendpage) {
		PRINT_ERROR("Socket for sid %llx doesn't support sendpage()",
			    (unsigned long long)session->sid);
		return -EINVAL;
	}
#endif

#if 0
	conn->sock->sk->sk_allocation = GFP_NOIO;
#endif
	conn->sock->sk->sk_user_data = conn;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	conn->sock->ops->setsockopt(conn->sock, SOL_TCP, TCP_NODELAY,
				    KERNEL_SOCKPTR(&opt), sizeof(opt));
	set_fs(oldfs);

	return 0;
}

void iscsi_tcp_conn_free(struct iscsi_conn *conn)
{
	fput(conn->file);
	conn->file = NULL;
	conn->sock = NULL;

	free_page((unsigned long)conn->read_iov);

	kmem_cache_free(iscsi_conn_cache, conn);
}

/* target_mutex supposed to be locked */
void conn_free(struct iscsi_conn *conn)
{
	struct iscsi_session *session = conn->session;

	TRACE_ENTRY();

	TRACE(TRACE_MGMT, "Freeing conn %p (sess=%p, %#Lx %u, initiator %s)",
	      conn, session, (unsigned long long)session->sid, conn->cid,
	      session->scst_sess->initiator_name);

	lockdep_assert_held(&conn->target->target_mutex);

	timer_delete_sync(&conn->rsp_timer);

	conn_sysfs_del(conn);

	sBUG_ON(atomic_read(&conn->conn_ref_cnt) != 0);
	sBUG_ON(!list_empty(&conn->cmd_list));
	sBUG_ON(!list_empty(&conn->write_list));
	sBUG_ON(!list_empty(&conn->write_timeout_list));
	sBUG_ON(conn->conn_reinst_successor);
	sBUG_ON(!test_bit(ISCSI_CONN_SHUTTINGDOWN, &conn->conn_aflags));

	/* Just in case if new conn gets freed before the old one */
	if (test_bit(ISCSI_CONN_REINSTATING, &conn->conn_aflags)) {
		struct iscsi_conn *c;

		TRACE_MGMT_DBG("Freeing being reinstated conn %p", conn);
		list_for_each_entry(c, &session->conn_list, conn_list_entry) {
			if (c->conn_reinst_successor == conn) {
				c->conn_reinst_successor = NULL;
				break;
			}
		}
	}

	list_del(&conn->conn_list_entry);

	conn->transport->iscsit_conn_free(conn);

	if (list_empty(&session->conn_list)) {
		sBUG_ON(session->sess_reinst_successor);
		session_free(session, true);
	}
}

int iscsi_init_conn(struct iscsi_session *session,
		    struct iscsi_kern_conn_info *info,
		    struct iscsi_conn *conn)
{
	int res;

	atomic_set(&conn->conn_ref_cnt, 0);
	conn->session = session;
	if (session->sess_reinstating)
		__set_bit(ISCSI_CONN_REINSTATING, &conn->conn_aflags);
	conn->cid = info->cid;
	conn->stat_sn = info->stat_sn;
	conn->exp_stat_sn = info->exp_stat_sn;
	conn->rd_state = ISCSI_CONN_RD_STATE_IDLE;
	conn->wr_state = ISCSI_CONN_WR_STATE_IDLE;

	conn->hdigest_type = session->sess_params.header_digest;
	conn->ddigest_type = session->sess_params.data_digest;
	res = digest_init(conn);
	if (res != 0)
		return res;

	conn->target = session->target;
	spin_lock_init(&conn->cmd_list_lock);
	INIT_LIST_HEAD(&conn->cmd_list);
	spin_lock_init(&conn->write_list_lock);
	INIT_LIST_HEAD(&conn->write_list);
	INIT_LIST_HEAD(&conn->write_timeout_list);
	timer_setup(&conn->rsp_timer, conn_rsp_timer_fn, 0);
	init_waitqueue_head(&conn->read_state_waitQ);
	init_completion(&conn->ready_to_free);
	INIT_LIST_HEAD(&conn->reinst_pending_cmd_list);
	INIT_LIST_HEAD(&conn->nop_req_list);
	spin_lock_init(&conn->nop_req_list_lock);

	conn->conn_thr_pool = session->sess_thr_pool;

	conn->nop_in_ttt = 0;
	INIT_DELAYED_WORK(&conn->nop_in_delayed_work,
			  conn_nop_in_delayed_work_fn);
	conn->last_rcv_time = jiffies;
	conn->data_rsp_timeout = session->tgt_params.rsp_timeout * HZ;
	conn->nop_in_interval = session->tgt_params.nop_in_interval * HZ;
	conn->nop_in_timeout = session->tgt_params.nop_in_timeout * HZ;
	if (conn->nop_in_interval > 0) {
		TRACE_DBG("Schedule Nop-In work for conn %p", conn);
		schedule_delayed_work(&conn->nop_in_delayed_work,
				      conn->nop_in_interval + ISCSI_ADD_SCHED_TIME);
	}

	return 0;
}
EXPORT_SYMBOL(iscsi_init_conn);

/* target_mutex supposed to be locked */
int iscsi_conn_alloc(struct iscsi_session *session, struct iscsi_kern_conn_info *info,
		     struct iscsi_conn **new_conn, struct iscsit_transport *t)
{
	struct iscsi_conn *conn;
	int res = 0;

	lockdep_assert_held(&session->target->target_mutex);

	conn = kmem_cache_zalloc(iscsi_conn_cache, GFP_KERNEL);
	if (!conn) {
		res = -ENOMEM;
		goto out_err;
	}

	TRACE(TRACE_MGMT,
	      "Creating connection %p for sid %#Lx, cid %u (initiator %s)",
	      conn, (unsigned long long)session->sid,
	      info->cid, session->scst_sess->initiator_name);

	conn->transport = t;

	/* Changing it, change ISCSI_CONN_IOV_MAX as well !! */
	conn->read_iov = (void *)get_zeroed_page(GFP_KERNEL);
	if (!conn->read_iov) {
		res = -ENOMEM;
		goto out_err_free_conn;
	}

	res = iscsi_init_conn(session, info, conn);
	if (res != 0)
		goto out_free_iov;

	conn->file = fget(info->fd);

	res = conn_setup_sock(conn);
	if (res != 0)
		goto out_fput;

	res = conn_sysfs_add(conn);
	if (res != 0)
		goto out_fput;

	list_add_tail(&conn->conn_list_entry, &session->conn_list);

	*new_conn = conn;

out:
	return res;

out_fput:
	fput(conn->file);

out_free_iov:
	free_page((unsigned long)conn->read_iov);

out_err_free_conn:
	kmem_cache_free(iscsi_conn_cache, conn);

out_err:
	goto out;
}

/* target_mutex supposed to be locked */
int __add_conn(struct iscsi_session *session, struct iscsi_kern_conn_info *info)
{
	struct iscsi_conn *conn, *new_conn = NULL;
	int err;
	bool reinstatement = false;
	struct iscsit_transport *t;

	lockdep_assert_held(&session->target->target_mutex);

	conn = conn_lookup(session, info->cid);
	if (conn && !test_bit(ISCSI_CONN_SHUTTINGDOWN, &conn->conn_aflags)) {
		/* conn reinstatement */
		reinstatement = true;
	} else if (!list_empty(&session->conn_list)) {
		err = -EEXIST;
		goto out;
	}

	if (session->sess_params.rdma_extensions)
		t = iscsit_get_transport(ISCSI_RDMA);
	else
		t = iscsit_get_transport(ISCSI_TCP);
	if (!t) {
		err = -ENOENT;
		goto out;
	}

	err = t->iscsit_conn_alloc(session, info, &new_conn, t);
	if (err != 0)
		goto out;

	if (reinstatement) {
		TRACE(TRACE_MGMT, "Reinstating conn (old %p, new %p)",
		      conn, new_conn);
		conn->conn_reinst_successor = new_conn;
		__set_bit(ISCSI_CONN_REINSTATING, &new_conn->conn_aflags);
		__mark_conn_closed(conn, 0);
	}

	err = t->iscsit_conn_activate(new_conn);

out:
	return err;
}

/* target_mutex supposed to be locked */
int __del_conn(struct iscsi_session *session, struct iscsi_kern_conn_info *info)
{
	struct iscsi_conn *conn;
	int err = -EEXIST;

	conn = conn_lookup(session, info->cid);
	if (!conn) {
		PRINT_WARNING("Connection %d not found", info->cid);
		return err;
	}

	PRINT_INFO("Deleting connection with initiator %s (%p)",
		   conn->session->initiator_name, conn);

	__mark_conn_closed(conn, ISCSI_CONN_ACTIVE_CLOSE | ISCSI_CONN_DELETING);

	return 0;
}

#ifdef CONFIG_SCST_EXTRACHECKS

void iscsi_extracheck_is_rd_thread(struct iscsi_conn *conn)
{
	if (unlikely(current != conn->rd_task)) {
		pr_emerg("conn %p rd_task != current %p (pid %d)\n",
			 conn, current, current->pid);
		while (in_softirq())
			local_bh_enable();
		pr_emerg("rd_state %x\n", conn->rd_state);
		pr_emerg("rd_task %p\n", conn->rd_task);
		if (conn->rd_task)
			pr_emerg("rd_task->pid %d\n", conn->rd_task->pid);
		BUG();
	}
}

void iscsi_extracheck_is_wr_thread(struct iscsi_conn *conn)
{
	if (unlikely(current != conn->wr_task)) {
		pr_emerg("conn %p wr_task != current %p (pid %d)\n",
			 conn, current, current->pid);
		while (in_softirq())
			local_bh_enable();
		pr_emerg("wr_state %x\n", conn->wr_state);
		pr_emerg("wr_task %p\n", conn->wr_task);
		pr_emerg("wr_task->pid %d\n", conn->wr_task->pid);
		BUG();
	}
}

#endif /* CONFIG_SCST_EXTRACHECKS */
