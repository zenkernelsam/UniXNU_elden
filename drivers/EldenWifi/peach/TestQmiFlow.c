/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Actual prepared function bodies are inserted by test_peach_flow.py.
 * Transport, allocation and kernel contexts below are host fixtures only. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
#define ATH12K_QMI_WLFW_PMU_PARAMS_MAX_V01 16
#define ATH12K_QMI_WLFW_PMU_PIN_NAME_MAX_LEN_V01 32
#define PWRSEQ_QCOM_WCN_PMU_PIN_NAME_LEN 32
#define GFP_KERNEL 0
#define ATH12K_DBG_QMI 0
#define ath12k_warn(...) ((void)0)
#define ath12k_info(...) ((void)0)
#define ath12k_dbg(...) ((void)0)
#define QMI_WLANFW_AUX_UC_SUPPORT_V01 4u
#define QMI_RESULT_SUCCESS_V01 0
#define QMI_ERR_HARDWARE_RESTRICTED_V01 0x53
#define WLFW_TME_LITE_PATCH_FILE_V01 0
#define QMI_WLANFW_TME_LITE_INFO_REQ_V01 0x5b
#define QMI_WLANFW_TME_LITE_INFO_REQ_MSG_V01_MAX_MSG_LEN 25
#define ATH12K_QMI_WLANFW_TIMEOUT_MS 5000
#define msecs_to_jiffies(x) (x)
enum {ATH12K_QMI_BDF_TYPE_REGDB,ATH12K_QMI_BDF_TYPE_ELF,ATH12K_QMI_BDF_TYPE_CALIBRATION};
struct qmi_wlanfw_pmu_param_v01 {u8 pin_name[32];u32 wake_volt_valid,wake_volt,sleep_volt_valid,sleep_volt;};
struct pwrseq_qcom_wcn_pmu_param {u8 pin_name[32];u32 wake_volt_valid,wake_volt,sleep_volt_valid,sleep_volt;};
struct qmi_wlanfw_cap_resp_msg_v01 {bool ol_cpr_cfg_valid;struct {u32 pmu_param_len;struct qmi_wlanfw_pmu_param_v01 pmu_param[16];} ol_cpr_cfg;};
struct m3_mem_region {u64 paddr;u32 size;};
struct ath12k_hw_params {struct {bool download_tme_lite,download_aux_ucode;} fw;bool download_calib;};
struct ath12k_base;
struct ath12k_qmi {struct ath12k_base *ab;struct m3_mem_region tme_lite_mem;int handle;struct {u64 fw_caps;} target;};
struct ath12k_base {void *dev;struct ath12k_hw_params *hw_params;struct ath12k_qmi qmi;};
struct qmi_wlanfw_tme_lite_info_req_msg_v01 {u32 tme_file;u64 addr;u32 size;};
struct qmi_wlanfw_tme_lite_info_resp_msg_v01 {struct {unsigned result,error;} resp;};
struct qmi_txn {struct qmi_wlanfw_tme_lite_info_resp_msg_v01 *response;};
static const unsigned qmi_wlanfw_tme_lite_info_resp_msg_v01_ei[1]={0},qmi_wlanfw_tme_lite_info_req_msg_v01_ei[1]={0};
enum {CAP=1,OLCPR,LOAD,INIT,SEND,WAIT,REGDB,BDF,M3,AUX,QDSS,CAL};
static unsigned calls[32],used,fail_stage,live_allocations,cancels,fw_result,fw_error;
static bool no_memory;
static struct qmi_wlanfw_cap_resp_msg_v01 capability;
static struct pwrseq_qcom_wcn_pmu_param received[16];static size_t received_count;
static int step(unsigned stage){assert(used<32);calls[used++]=stage;return stage==fail_stage?-(int)(100+stage):0;}
static void *kcalloc(size_t count,size_t bytes,int ignored){(void)ignored;if(no_memory)return NULL;void *p=calloc(count,bytes);if(p)++live_allocations;return p;}
static void kfree(void *p){if(p){assert(live_allocations);--live_allocations;free(p);}}
static int pwrseq_qcom_wcn_set_ol_cpr(void *dev,const struct pwrseq_qcom_wcn_pmu_param *p,size_t count){assert(dev&&count<=16);memcpy(received,p,count*sizeof(*p));received_count=count;return step(OLCPR);}
// @APPLY_OLCPR@
static int ath12k_qmi_request_target_cap(struct ath12k_base *ab){int rc=step(CAP);if(rc)return rc;return ab->hw_params->fw.download_tme_lite?ath12k_qmi_apply_ol_cpr(ab,&capability):0;}
static int ath12k_qmi_tme_lite_load(struct ath12k_base *ab){ab->qmi.tme_lite_mem=(struct m3_mem_region){0x12345000,56752};return step(LOAD);}
static int qmi_txn_init(int *h,struct qmi_txn *txn,const unsigned *ei,void *response){(void)h;(void)ei;txn->response=response;return step(INIT);}
static int qmi_send_request(int *h,void *addr,struct qmi_txn *txn,unsigned id,unsigned length,const unsigned *ei,const void *request){
    (void)h;(void)addr;(void)txn;(void)ei;const struct qmi_wlanfw_tme_lite_info_req_msg_v01 *r=request;
    assert(id==0x5b&&length==25&&r->tme_file==0&&r->addr==0x12345000&&r->size==56752);return step(SEND);
}
static void qmi_txn_cancel(struct qmi_txn *txn){(void)txn;++cancels;}
static int qmi_txn_wait(struct qmi_txn *txn,unsigned timeout){assert(timeout==5000);txn->response->resp.result=fw_result;txn->response->resp.error=fw_error;return step(WAIT);}
// @TME_SEND@
static int ath12k_qmi_load_bdf_qmi(struct ath12k_base *ab,int kind){(void)ab;return step(kind==ATH12K_QMI_BDF_TYPE_REGDB?REGDB:kind==ATH12K_QMI_BDF_TYPE_ELF?BDF:CAL);}
static int ath12k_qmi_wlanfw_m3_info_send(struct ath12k_base *ab){(void)ab;return step(M3);}
static int ath12k_qmi_wlanfw_aux_uc_info_send(struct ath12k_base *ab){(void)ab;return step(AUX);}
static int ath12k_qmi_wlanfw_qdss_config_send(struct ath12k_base *ab){(void)ab;return step(QDSS);}
// @LOAD_BDF@
static void reset(void){assert(!live_allocations);used=fail_stage=cancels=fw_result=fw_error=received_count=0;no_memory=false;memset(received,0,sizeof(received));}
int main(void){
    struct ath12k_hw_params hw={.fw={true,true}};struct ath12k_base ab={.dev=&hw,.hw_params=&hw};ab.qmi.ab=&ab;ab.qmi.target.fw_caps=4;
    capability.ol_cpr_cfg_valid=true;capability.ol_cpr_cfg.pmu_param_len=1;
    for(unsigned i=0;i<16;++i)memcpy(capability.ol_cpr_cfg.pmu_param[i].pin_name,"VDDD_AON_0P9",13);
    capability.ol_cpr_cfg.pmu_param[0].wake_volt_valid=1;capability.ol_cpr_cfg.pmu_param[0].wake_volt=876;
    const unsigned order[]={CAP,OLCPR,LOAD,INIT,SEND,WAIT,REGDB,BDF,M3,AUX,QDSS};
    for(unsigned fail=0;fail<=QDSS;++fail){reset();fail_stage=fail;int rc=ath12k_qmi_event_load_bdf(&ab.qmi);
        assert(rc==(fail?-(int)(100+fail):0));assert(used==(fail?fail:11));assert(!memcmp(calls,order,used*sizeof(*calls)));
        assert(cancels==(fail==SEND));assert(!live_allocations);
    }
    reset();fw_result=1;fw_error=0x53;assert(!ath12k_qmi_event_load_bdf(&ab.qmi)&&used==11); // explicit nonfatal restriction, not load proof
    reset();fw_result=1;fw_error=0x54;assert(ath12k_qmi_event_load_bdf(&ab.qmi)==-EINVAL&&used==6);
    reset();ab.qmi.target.fw_caps=0;assert(!ath12k_qmi_event_load_bdf(&ab.qmi)&&used==10&&calls[9]==QDSS);
    reset();hw.fw.download_tme_lite=false;assert(!ath12k_qmi_event_load_bdf(&ab.qmi));
    const unsigned legacy[]={CAP,REGDB,BDF,M3,AUX};assert(used==5&&!memcmp(calls,legacy,sizeof(legacy)));
    hw.fw.download_tme_lite=true;ab.qmi.target.fw_caps=4;
    reset();capability.ol_cpr_cfg_valid=false;assert(ath12k_qmi_apply_ol_cpr(&ab,&capability)==-ENODATA&&used==0);
    capability.ol_cpr_cfg_valid=true;capability.ol_cpr_cfg.pmu_param_len=17;assert(ath12k_qmi_apply_ol_cpr(&ab,&capability)==-EINVAL&&used==0);
    capability.ol_cpr_cfg.pmu_param_len=0;assert(ath12k_qmi_apply_ol_cpr(&ab,&capability)==-EINVAL&&used==0);
    capability.ol_cpr_cfg.pmu_param_len=16;no_memory=true;assert(ath12k_qmi_apply_ol_cpr(&ab,&capability)==-ENOMEM&&used==0);
    reset();assert(!ath12k_qmi_apply_ol_cpr(&ab,&capability)&&received_count==16&&!live_allocations);
    assert(!memcmp(received,capability.ol_cpr_cfg.pmu_param,sizeof(received)));
    puts("Prepared QMI bodies: mandatory failure ordering, cancellation,0x53-only exception, non-Peach AUX and OL-CPR bounds/copy/lifetime passed");
}
