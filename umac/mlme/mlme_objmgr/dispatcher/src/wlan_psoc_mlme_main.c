/*
 * Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: Implements PSOC MLME APIs
 */

#if defined(IPA_OFFLOAD) && defined(QCA_IPA_LL_TX_FLOW_CONTROL)
#include <wlan_ipa_ucfg_api.h>
#endif
#include <qdf_module.h>
#include <wlan_objmgr_cmn.h>
#include <wlan_objmgr_global_obj.h>
#include <wlan_mlme_dbg.h>
#include <include/wlan_mlme_cmn.h>
#include <include/wlan_psoc_mlme.h>
#include <wlan_psoc_mlme_main.h>
#include <wlan_psoc_mlme_api.h>

struct psoc_mlme_obj *mlme_psoc_get_priv(struct wlan_objmgr_psoc *psoc)
{
	struct psoc_mlme_obj *psoc_mlme;

	psoc_mlme = wlan_objmgr_psoc_get_comp_private_obj(psoc,
							  WLAN_UMAC_COMP_MLME);
	if (!psoc_mlme) {
		mlme_err("PSOC MLME component object is NULL");
		return NULL;
	}

	return psoc_mlme;
}

qdf_export_symbol(mlme_psoc_get_priv);

static QDF_STATUS mlme_psoc_obj_create_handler(struct wlan_objmgr_psoc *psoc,
					       void *arg)
{
	struct psoc_mlme_obj *psoc_mlme;
	QDF_STATUS status = QDF_STATUS_SUCCESS;

	psoc_mlme = qdf_mem_malloc(sizeof(struct psoc_mlme_obj));
	if (!psoc_mlme) {
		mlme_err("Failed to allocate PSOS mlme Object");
		return QDF_STATUS_E_NOMEM;
	}

	psoc_mlme->psoc = psoc;

	status = mlme_psoc_ops_ext_hdl_create(psoc_mlme);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlme_err("Failed to allocate psoc ext handle");
		goto init_failed;
	}

	status = wlan_objmgr_psoc_component_obj_attach(psoc,
						       WLAN_UMAC_COMP_MLME,
						       psoc_mlme,
						       QDF_STATUS_SUCCESS);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlme_err("Failed to attach psoc_ctx with psoc");
		goto init_failed;
	}

	return QDF_STATUS_SUCCESS;
init_failed:
	qdf_mem_free(psoc_mlme);

	return status;
}

static QDF_STATUS mlme_psoc_obj_destroy_handler(struct wlan_objmgr_psoc *psoc,
						void *arg)
{
	struct psoc_mlme_obj *psoc_mlme;

	psoc_mlme = mlme_psoc_get_priv(psoc);
	if (!psoc_mlme) {
		mlme_err("PSOC MLME component object is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	wlan_objmgr_psoc_component_obj_detach(psoc, WLAN_UMAC_COMP_MLME,
					      psoc_mlme);

	mlme_psoc_ops_ext_hdl_destroy(psoc_mlme);

	qdf_mem_free(psoc_mlme);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wlan_psoc_mlme_init(void)
{
	if (wlan_objmgr_register_psoc_create_handler
				(WLAN_UMAC_COMP_MLME,
				 mlme_psoc_obj_create_handler, NULL)
						!= QDF_STATUS_SUCCESS)
		return QDF_STATUS_E_FAILURE;

	if (wlan_objmgr_register_psoc_destroy_handler
				(WLAN_UMAC_COMP_MLME,
				 mlme_psoc_obj_destroy_handler, NULL)
						!= QDF_STATUS_SUCCESS) {
		if (wlan_objmgr_unregister_psoc_create_handler
					(WLAN_UMAC_COMP_MLME,
					 mlme_psoc_obj_create_handler, NULL)
						!= QDF_STATUS_SUCCESS)
			return QDF_STATUS_E_FAILURE;

		return QDF_STATUS_E_FAILURE;
	}

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wlan_psoc_mlme_deinit(void)
{
	if (wlan_objmgr_unregister_psoc_create_handler
				(WLAN_UMAC_COMP_MLME,
				 mlme_psoc_obj_create_handler, NULL)
					!= QDF_STATUS_SUCCESS)
		return QDF_STATUS_E_FAILURE;

	if (wlan_objmgr_unregister_psoc_destroy_handler
				(WLAN_UMAC_COMP_MLME,
				 mlme_psoc_obj_destroy_handler, NULL)
						!= QDF_STATUS_SUCCESS)
		return QDF_STATUS_E_FAILURE;

	return QDF_STATUS_SUCCESS;
}

#if defined(IPA_OFFLOAD) && defined(QCA_IPA_LL_TX_FLOW_CONTROL)
static
void wlan_psoc_mlme_ipa_evt_wq_handler(void *ctx)
{
	struct wlan_ipa_evt_wq_args *ipa_ctx, *ipa_ctx_next;
	struct psoc_mlme_obj *psoc_mlme = (struct psoc_mlme_obj *)ctx;

	TAILQ_HEAD(, wlan_ipa_evt_wq_args) ipa_ctx_list;

	TAILQ_INIT(&ipa_ctx_list);

	qdf_spin_lock_bh(&psoc_mlme->ipa_evt_wq->list_lock);

	TAILQ_CONCAT(&ipa_ctx_list, &psoc_mlme->ipa_evt_wq->list, list_elem);
	qdf_spin_unlock_bh(&psoc_mlme->ipa_evt_wq->list_lock);

	TAILQ_FOREACH_SAFE(ipa_ctx, &ipa_ctx_list, list_elem, ipa_ctx_next) {
		TAILQ_REMOVE(&ipa_ctx_list, ipa_ctx, list_elem);
		if (!ipa_ctx->pdev_obj) {
			qdf_mem_free(ipa_ctx);
			continue;
		}
		ucfg_ipa_wlan_evt(ipa_ctx->pdev_obj, ipa_ctx->net_dev,
				  ipa_ctx->device_mode, ipa_ctx->vdev_id,
				  ipa_ctx->event, ipa_ctx->mac_addr,
				  ipa_ctx->ch_freq);

		/* Clean Interface for STA/AP disconnect event */
		if ((ipa_ctx->event == WLAN_IPA_AP_DISCONNECT) ||
		    (ipa_ctx->event == WLAN_IPA_STA_DISCONNECT))
			ucfg_ipa_flush_pending_vdev_events(ipa_ctx->pdev_obj,
							   ipa_ctx->vdev_id);

		if (ipa_ctx->event == WLAN_IPA_AP_DISCONNECT)
			ucfg_ipa_cleanup_dev_iface(ipa_ctx->pdev_obj,
						   ipa_ctx->net_dev);

		wlan_objmgr_vdev_release_ref(ipa_ctx->vdev, WLAN_IPA_ID);
		qdf_mem_free(ipa_ctx);
	}
}

QDF_STATUS wlan_psoc_mlme_ipa_evt_wq_attach(struct wlan_objmgr_psoc *psoc)
{
	struct psoc_mlme_obj *psoc_mlme = wlan_psoc_mlme_get_cmpt_obj(psoc);

	psoc_mlme->ipa_evt_wq = qdf_mem_malloc(sizeof(struct wlan_ipa_evt_wq));

	if (!psoc_mlme->ipa_evt_wq)
		return QDF_STATUS_E_FAILURE;

	TAILQ_INIT(&psoc_mlme->ipa_evt_wq->list);

	qdf_create_work(0, &psoc_mlme->ipa_evt_wq->work,
			wlan_psoc_mlme_ipa_evt_wq_handler, psoc_mlme);

	psoc_mlme->ipa_evt_wq->work_queue =
		qdf_alloc_unbound_workqueue("wlan_ipa_evt_work_queue");

	if (!psoc_mlme->ipa_evt_wq->work_queue)
		goto fail;

	qdf_spinlock_create(&psoc_mlme->ipa_evt_wq->list_lock);
	return QDF_STATUS_SUCCESS;

fail:
	qdf_flush_work(&psoc_mlme->ipa_evt_wq->work);
	qdf_disable_work(&psoc_mlme->ipa_evt_wq->work);
	qdf_mem_free(psoc_mlme->ipa_evt_wq);
	psoc_mlme->ipa_evt_wq = NULL;
	return QDF_STATUS_E_FAILURE;
}

qdf_export_symbol(wlan_psoc_mlme_ipa_evt_wq_attach);

void wlan_psoc_mlme_ipa_evt_wq_detach(struct wlan_objmgr_psoc *psoc)
{
	struct wlan_ipa_evt_wq_args *ctx, *ctx_next;
	struct psoc_mlme_obj *psoc_mlme = wlan_psoc_mlme_get_cmpt_obj(psoc);

	if (!(psoc_mlme->ipa_evt_wq && psoc_mlme->ipa_evt_wq->work_queue))
		return;

	qdf_flush_workqueue(0, psoc_mlme->ipa_evt_wq->work_queue);
	qdf_destroy_workqueue(0, psoc_mlme->ipa_evt_wq->work_queue);
	qdf_flush_work(&psoc_mlme->ipa_evt_wq->work);
	qdf_disable_work(&psoc_mlme->ipa_evt_wq->work);
	qdf_spin_lock_bh(&psoc_mlme->ipa_evt_wq->list_lock);

	TAILQ_FOREACH_SAFE(ctx, &psoc_mlme->ipa_evt_wq->list, list_elem,
			   ctx_next) {
		TAILQ_REMOVE(&psoc_mlme->ipa_evt_wq->list, ctx, list_elem);
		wlan_objmgr_vdev_release_ref(ctx->vdev, WLAN_IPA_ID);
		qdf_mem_free(ctx);
	}

	qdf_spin_unlock_bh(&psoc_mlme->ipa_evt_wq->list_lock);
	qdf_spinlock_destroy(&psoc_mlme->ipa_evt_wq->list_lock);
	qdf_mem_free(psoc_mlme->ipa_evt_wq);
	psoc_mlme->ipa_evt_wq = NULL;
}

qdf_export_symbol(wlan_psoc_mlme_ipa_evt_wq_detach);
#endif
