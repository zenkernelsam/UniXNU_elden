"""Derive client hooks in the original sole Elden IPCRTR/QRTR owner."""
from pathlib import Path
import hashlib, json

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
PINS = {
    'drivers/EldenPmicGlink/ShengQrtr.c': '6584dd43500281ec3606665fda56b8b5394b2c559fcbcea4ebaea124fd6108e0',
    'drivers/EldenPmicGlink/QcomQrtr.c': 'd22ff8572b93424f1f03138204a44e10b444d0cb62b6be4f1c57b7d3b2ddbc74',
    'drivers/EldenPmicGlink/QcomQrtr.h': '3ead40f5f996497224ebef92e2e67820aa8372bab0a21646c15df954baba84f1',
}

def derive():
    data = {}
    for name, digest in PINS.items():
        body = (ROOT / name).read_bytes()
        if hashlib.sha256(body).hexdigest() != digest:
            raise ValueError('Original QRTR input changed: ' + name)
        data[Path(name).name] = body.decode().replace('\r\n','\n')
    donor = json.loads((HERE / 'qrtr-source.json').read_text())
    for row in donor['files']:
        body = (HERE / 'upstream' / row['file']).read_bytes()
        if len(body) != row['bytes'] or hashlib.sha256(body).hexdigest() != row['sha256']:
            raise ValueError('Qualified QRTR donor changed')
        if Path(row['path']).name in ('QcomQrtr.c', 'QcomQrtr.h'):
            data[Path(row['path']).name] = body.decode().replace('\r\n','\n')
    changes = []
    def patch(name, before, after, reason):
        if data[name].count(before) != 1:
            raise ValueError('Unique QRTR source anchor changed: ' + reason)
        data[name] = data[name].replace(before, after)
        changes.append({'file': name, 'reason': reason})

    patch('QcomQrtr.c',
          '    const uint32_t padded = (length + 3) & ~3u;',
          '    if(length>UINT32_MAX-3u)return 0;\n    const uint32_t padded = (length + 3) & ~3u;',
          'pure frame padding overflow rejects before output')
    patch('QcomQrtr.c',
          '    if (length < QCOM_QRTR_HEADER || le32(packet) != 1)',
          '    if (!packet || length < QCOM_QRTR_HEADER || le32(packet) != 1)',
          'invalid packet pointer rejected')
    patch('QcomQrtr.c',
          '    if (size > length - QCOM_QRTR_HEADER)',
          '    if (size > length - QCOM_QRTR_HEADER || (le32(packet+24)!=q->local_node && le32(packet+24)!=QCOM_QRTR_NODE_BROADCAST))',
          'other destination never reaches local sensor clients')
    patch('QcomQrtr.c', '    if (size < 4) {q->rx_bad++; return -1;}',
          '    if (size < 4 || le32(data)!=type || ((type==QCOM_QRTR_NEW_SERVER||type==QCOM_QRTR_DEL_SERVER)&&size<20)) {q->rx_bad++; return -1;}',
          'control command and required payload checked before service mutation')
    patch('QcomQrtr.c', '    case QCOM_QRTR_HELLO:\n        q->remote_node = src_node;',
          '    case QCOM_QRTR_HELLO:\n        if(q->hello_received&&q->remote_node!=src_node)qcom_qrtr_forget_remote(q);\n        q->remote_node = src_node;',
          'different actual HELLO peer withdraws old service identities')
    # Donor supplies the correct RESUME_TX destination and receiver identity.
    # Add bounded retry debt because original ADSP send can refuse when full.
    patch('QcomQrtr.h', '    uint32_t remote_full;   /* announcements dropped: the list was full */',
          '''    uint32_t remote_full;   /* announcements dropped: the list was full */
    struct {uint32_t node,from,to;} elden_resume[16];
    uint32_t elden_resume_count,elden_resume_full;''',
          'bounded owed flow-control replies; rebuild original layout consumers')
    patch('QcomQrtr.h', '/* ---- QMI messages carried in data packets:',
          'void elden_qrtr_service(QcomQrtr *);\n\n/* ---- QMI messages carried in data packets:',
          'same DSP executor retries owed resumes without another timer/router')
    patch('QcomQrtr.c', 'static void announce(QcomQrtr *q, unsigned i)',
          '''static int elden_resume(QcomQrtr *q,uint32_t node,uint32_t from,uint32_t to){
    uint8_t packet[20]={0};put32(packet,QCOM_QRTR_RESUME_TX);
    put32(packet+4,q->local_node);put32(packet+8,from);
    if(emit(q,QCOM_QRTR_RESUME_TX,from,node,to,packet,sizeof packet))return -1;
    q->resumes_sent++;return 0;
}
void elden_qrtr_service(QcomQrtr *q){
    while(q->elden_resume_count){
        if(elden_resume(q,q->elden_resume[0].node,q->elden_resume[0].from,q->elden_resume[0].to))return;
        q->elden_resume_count--;
        for(uint32_t i=0;i<q->elden_resume_count;i++)q->elden_resume[i]=q->elden_resume[i+1];
    }
}
static void elden_owe_resume(QcomQrtr *q,uint32_t node,uint32_t from,uint32_t to){
    for(uint32_t i=0;i<q->elden_resume_count;i++)
        if(q->elden_resume[i].node==node&&q->elden_resume[i].from==from&&q->elden_resume[i].to==to){elden_qrtr_service(q);return;}
    if(!elden_resume(q,node,from,to))return;
    if(q->elden_resume_count==16){q->elden_resume_full++;return;}
    uint32_t i=q->elden_resume_count++;q->elden_resume[i].node=node;q->elden_resume[i].from=from;q->elden_resume[i].to=to;
}

static void announce(QcomQrtr *q, unsigned i)''',
          'retry exact receiver-address flow-control acknowledgement after backpressure')
    start = data['QcomQrtr.c'].index('            uint8_t pkt[CTRL_BYTES] = {0};', data['QcomQrtr.c'].index('if (confirm)'))
    end = data['QcomQrtr.c'].index('\n        }', start)
    data['QcomQrtr.c'] = data['QcomQrtr.c'][:start] + '            elden_owe_resume(q,src_node,dst_port,src_port);' + data['QcomQrtr.c'][end:]
    changes.append({'file': 'QcomQrtr.c', 'reason': 'RESUME_TX refusal stays owed instead of silently stalling sender'})
    patch('QcomQrtr.c', '    /* last first, so each report sees the list without the server it reports */',
          '    q->elden_resume_count=0;\n    /* last first, so each report sees the list without the server it reports */',
          'old link flow debt cannot cross an observed BYE/reset')
    patch('QcomQrtr.c', '        q->remote[i] = q->remote[--q->remote_count];',
          '        if(old_service!=service||old_instance!=instance)return; // stale DEL_SERVER cannot delete its replacement\n        q->remote[i] = q->remote[--q->remote_count];',
          'DEL_SERVER must name the exact current service/instance at that address')

    patch('ShengQrtr.c', '#include "ShengAdsp.h"',
          '#include "ShengAdsp.h"\n#include "QrtrPorts.h"', 'explicit epoch client and existing-router hooks')
    patch('ShengQrtr.c', 'static ShengAdspChannel *gChannel;',
          '''static ShengAdspChannel *gChannel;
static uint32_t gChannelEpoch;
static void clientSnapshot(void){
    EldenQrtrServer servers[QCOM_QRTR_MAX_REMOTE];
    for(uint32_t i=0;i<gQrtr.remote_count;i++){
        servers[i]=(EldenQrtrServer){gQrtr.remote[i].service,gQrtr.remote[i].instance,gQrtr.remote[i].node,gQrtr.remote[i].port,0};
    }
    if(gQrtr.remote_full||gQrtr.elden_resume_full){elden_qrtr_ports_fault(-75);return;}
    elden_qrtr_ports_snapshot(gChannelEpoch,gQrtr.hello_received,gQrtr.remote_node,servers,gQrtr.remote_count);
}
static void clientServers(void *context,QcomQrtr *q,int up,uint32_t service,uint32_t instance,uint32_t node,uint32_t port){
    (void)context;(void)q;(void)up;(void)service;(void)instance;(void)node;(void)port;clientSnapshot();
}
static void clientReceive(void *context,QcomQrtr *q,uint32_t port,uint32_t node,uint32_t from,const uint8_t *data,uint32_t length){
    (void)context;(void)q;elden_qrtr_ports_receive(gChannelEpoch,port,node,from,data,length);
}
void elden_qrtr_original_service(void){
    if(!gChannel)return;elden_qrtr_service(&gQrtr);clientSnapshot();
}''', 'existing sole producer updates borrowed server/client view and owed replies')
    patch('ShengQrtr.c', 'return sheng_adsp_send(gChannel, packet, length) ? -1 : 0;',
          'return sheng_adsp_send_epoch(gChannel, gChannelEpoch, packet, length) ? -1 : 0;',
          'all queued packets carry actual original IPCRTR channel epoch')
    patch('ShengQrtr.c', 'static void linkState(void *context, int up)',
          'static void linkState(void *context, uint32_t epoch, int up)', 'original channel state callback carries epoch')
    patch('ShengQrtr.c', '    (void)context;\n    if (up) {',
          '    (void)context;gChannelEpoch=epoch;elden_qrtr_ports_link(epoch,up);\n    gQrtr.hello_received=0;\n    qcom_qrtr_forget_remote(&gQrtr);\n    if (up) {',
          'old service and flow identities withdrawn on observed link state')
    patch('ShengQrtr.c', 'static void linkReceive(void *context, const uint8_t *data, uint32_t length)',
          'static void linkReceive(void *context, uint32_t epoch, const uint8_t *data, uint32_t length)',
          'actual same-epoch receive producer')
    patch('ShengQrtr.c', '    qcom_qrtr_receive(&gQrtr, data, length);',
          '    if(epoch!=gChannelEpoch)return;\n    if(qcom_qrtr_receive(&gQrtr,data,length))elden_qrtr_ports_fault(-84);\n    clientSnapshot();',
          'parse/epoch failures never publish valid sensor payload')
    patch('ShengQrtr.c', '    static const ShengAdspClient client = {linkState, linkReceive};',
          '    static const EldenAdspEpochClient client = {linkState, linkReceive};\n    uint32_t size=1024,count=8;\n    int configured=elden_qrtr_ports_rx_config(&size,&count);\n    if(configured<0){native_qrtr_stats[0]=-1;return;}',
          'legacy intents retained; production sensor intents require original-owner qualification before start')
    patch('ShengQrtr.c', '    gChannel = sheng_adsp_channel("IPCRTR", 1024, 8, 0, &client, NULL);',
          '    qcom_qrtr_watch(&gQrtr,clientServers,clientReceive,NULL);\n    gChannel=elden_adsp_channel_epoch("IPCRTR",size,count,0,&client,NULL);\n    if(configured&&elden_qrtr_ports_bind(gChannel,size)){native_qrtr_stats[0]=-1;return;}',
          'use sole existing registration; never start a second IPCRTR')
    # All original locator/domain/counter functions stay in this same source.
    return data, changes
