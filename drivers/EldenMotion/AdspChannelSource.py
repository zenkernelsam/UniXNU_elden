"""Derive owned-client/close APIs from exact current Elden ADSP/GLINK sources.

Keep original firmware/power/IRQ bring-up and old client entrypoints. Changes
extend the sole original registry and worker; no separate IPCRTR or controller.
"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
PINS={
 'drivers/EldenPmicGlink/AdspProbe.cpp':'b7982488455d6c2528a0a9d9db88a99cc4327c6136f8f945641e46bb4638912e',
 'drivers/EldenPmicGlink/ShengAdsp.h':'cd8ace83a55e99503c24691b6ddef294d9dbf2d416e18f85a51e8f4a30440dfe',
 'drivers/EldenPmicGlink/QcomGlink.h':'7e617c764e11ba7ca08e9976e39c35710515a16613ea66f3e845da538ad2df07',
 'profiles/v91/native-build/extra/3d7e18c2cae2/QcomGlink.c':'3d7e18c2cae2c6136b1b7bf43f8d69e49c99ed8e60d3ee7cd98c37891df9b1ba'}
def derive():
    data={}
    for p,d in PINS.items():
        b=(ROOT/p).read_bytes()
        if hashlib.sha256(b).hexdigest()!=d:raise ValueError('Qualified ADSP/GLINK source changed: '+p)
        data[Path(p).name]=b.decode()
    # Same-SM8750 donor fixes late registration, RX_DONE debt and atomic CLOSE
    # response; use its whole protocol, excluding any board/ADSP runtime.
    donor=json.loads((HERE/'glink-source.json').read_text())
    for row in donor['files']:
        b=(HERE/'upstream'/row['file']).read_bytes()
        if len(b)!=row['bytes'] or hashlib.sha256(b).hexdigest()!=row['sha256']:raise ValueError('Qualified donor GLINK changed')
        data[Path(row['path']).name]=b.decode()
    deltas=[]
    def patch(name,before,after,why):
        if data[name].count(before)!=1:raise ValueError('Unique ADSP/GLINK anchor changed: '+why)
        data[name]=data[name].replace(before,after);deltas.append({'file':name,'reason':why})
    patch('QcomGlink.h','#define QCOM_GLINK_MAX_CHANNELS 8u','#define QCOM_GLINK_MAX_CHANNELS 6u','keep original Elden implemented channel capacity')
    patch('QcomGlink.h','    QcomGlinkClient client;',
          '    uint8_t elden_closing,elden_close_sent,elden_close_acked,elden_peer_closed,elden_ever_opened;\n    QcomGlinkClient client;','owned channel close fields; every layout owner recompiled')
    patch('QcomGlink.c','    c->open_sent = 1;\n    return QcomGlinkOk;',
          '''    c->open_sent=1;c->elden_ever_opened=1;
    if(!c->elden_closing)c->elden_close_sent=c->elden_close_acked=c->elden_peer_closed=0;
    return QcomGlinkOk;''','actual local OPEN separates never-opened from prior reset')
    patch('QcomGlink.c','        if (!c->used) continue;\n        if (c->ack_owed) {',
          '''        if (!c->used) continue;
        if(c->elden_closing){
            /* A peer OPEN already received must finish that original
             * handshake before our CLOSE. Never publish it up while closing. */
            if(c->remote_open&&c->open_sent!=1){
                if(c->ack_owed){if(command(g,QCOM_GLINK_OPEN_ACK,c->rcid,0)){g->tx_blocked=1;return;}c->ack_owed=0;}
                if(send_open(g,c)){g->tx_blocked=1;return;}
            }
            if(!c->elden_close_sent&&c->open_sent==1){
                if(command(g,QCOM_GLINK_CLOSE,c->lcid,0)){g->tx_blocked=1;return;}
                c->elden_close_sent=1;
            }
            continue; // retained closed registration never reopens
        }
        if (c->ack_owed) {''','original worker sends actual CLOSE once and suppresses reopened progression')
    patch('QcomGlink.c','''        if (!c) {
            g->unknown_opens++;''','''        if(c&&c->elden_closing){
            if(c->elden_peer_closed||c->open_sent!=1){g->dropped++;}
            else{
                if(c->remote_open&&c->rcid!=param1)return QcomGlinkCorrupt;
                if(command(g,QCOM_GLINK_OPEN_ACK,param1,0))return 0;
                c->rcid=param1;c->remote_open=1;
            }
            rx_advance(&g->rx,align8(8+n));g->rx_messages++;return 1;
        }
        if (!c) {
            g->unknown_opens++;''','reject remote OPEN on retained closed registration')
    patch('QcomGlink.c','''    case QCOM_GLINK_CLOSE:
        /* 13T: the CLOSE_ACK and our own CLOSE go out together or not at all (a CLOSE handled again would be acknowledged twice). */
        c = by_rcid(g, param1);''',
          '''    case QCOM_GLINK_CLOSE:
        c=by_rcid(g,param1);
        if(c&&c->elden_closing){
            uint32_t required=c->elden_close_sent?8u:16u;
            if(qcom_glink_tx_avail(&g->tx)<required){g->tx_blocked=1;return 0;}
            (void)command(g,QCOM_GLINK_CLOSE_ACK,param1,0);
            if(!c->elden_close_sent){(void)command(g,QCOM_GLINK_CLOSE,c->lcid,0);c->elden_close_sent=1;}
            c->elden_peer_closed=1;const int was=c->up;reset_channel(c);
            if(was&&c->client.state)c->client.state(c->context,c,0);
            break;
        }
        c = by_rcid(g, param1);''','owned peer CLOSE ACK and local CLOSE atomic FIFO commit')
    patch('QcomGlink.c','    case QCOM_GLINK_CLOSE_ACK:\n        break;',
          '    case QCOM_GLINK_CLOSE_ACK:\n        c=by_lcid(g,param1);if(c&&c->elden_close_sent)c->elden_close_acked=1;\n        break;',
          'actual matching CLOSE_ACK recorded; wrong/unsolicited ACK ignored')
    patch('QcomGlink.c','            c->rcid = param1;\n            c->remote_open = 1;',
          '            c->rcid=param1;c->remote_open=1;\n            c->elden_close_sent=c->elden_close_acked=c->elden_peer_closed=0;',
          'fresh original remote OPEN cannot inherit old close proof')
    patch('QcomGlink.c','''            if (c->open_sent == 1) (void)command(g, QCOM_GLINK_CLOSE, c->lcid, 0);
            reset_channel(c);''',
          '''            c->elden_peer_closed=1;
            if(c->open_sent==1){(void)command(g,QCOM_GLINK_CLOSE,c->lcid,0);c->elden_close_sent=1;}
            else c->elden_close_sent=c->elden_close_acked=1; // no local OPEN in this session
            reset_channel(c);''','remote-driven legacy CLOSE retains actual outstanding local ACK state')
    patch('QcomGlink.c','    if (g->broken || !c->up) return QcomGlinkNotOpen;',
          '    if (g->broken || !c->up || c->elden_closing) return QcomGlinkNotOpen;','no transport send after close admission')
    data['QcomGlink.c']+='\n'+(HERE/'GlinkClosePublic.inc').read_text()
    patch('QcomGlink.h','/* FIFO primitives, exposed for the tests. */',
          '''/* Sole actual worker close extension.0only request latch;1status means
 * both endpoint handshakes observed, not physical DMA stopped. */
int elden_glink_close_request(QcomGlink *,QcomGlinkChannel *);
int elden_glink_close_status(const QcomGlink *,const QcomGlinkChannel *,uint32_t *,uint32_t *,uint32_t *);
/* FIFO primitives, exposed for the tests. */''','separate endpoint close proof API')
    patch('AdspProbe.cpp','#include "ShengAdsp.h"','#include "ShengAdsp.h"\n#include "AdspChannel.h"','owned epoch callback API; legacy struct unchanged')
    patch('AdspProbe.cpp','constexpr unsigned kClients = 4, kQueue = 8;',
          'constexpr unsigned kClients = QCOM_GLINK_MAX_CHANNELS, kQueue = 8;','use implemented sole GLINK channel capacity including FastRPC')
    patch('AdspProbe.cpp','    bool connected;\n};',
          '    bool connected;\n    EldenAdspEpochClient epoch_client;bool epoch_callbacks;\n    uint32_t close_epoch;EldenAdspChannelStatus owned_status;\n};','retain legacy callbacks plus epoch-specific registry state')
    begin=data['AdspProbe.cpp'].index('void clientState(');end=data['AdspProbe.cpp'].index('// ---- the bring-up ----',begin)
    data['AdspProbe.cpp']=data['AdspProbe.cpp'][:begin]+'#include "AdspRegistryMethods.inc"\n\n'+data['AdspProbe.cpp'][end:]
    deltas.append({'file':'AdspProbe.cpp','reason':'actual original registry callback/pump methods with serialized owned closing'})
    begin=data['AdspProbe.cpp'].index('extern "C" ShengAdspChannel *sheng_adsp_channel(')
    end=data['AdspProbe.cpp'].index('extern "C" void sheng_adsp_probe(',begin)
    data['AdspProbe.cpp']=data['AdspProbe.cpp'][:begin]+'#include "AdspPublicMethods.inc"\n'+data['AdspProbe.cpp'][end:]
    deltas.append({'file':'AdspProbe.cpp','reason':'legacy entrypoints use same common registry; new owned epoch/status/close APIs'})
    patch('AdspProbe.cpp','void pumpSends();\nvoid serviceGlink()',
          'void pumpSends();\nvoid serviceOwnedChannels();\nvoid serviceGlink()','original DSP worker services owned close requests')
    patch('AdspProbe.cpp','    qcom_glink_poll(&gGlink);\n    pumpSends();',
          '    serviceOwnedChannels();\n    if(qcom_glink_poll(&gGlink)<0)qcom_glink_stop(&gGlink);\n    serviceOwnedChannels();\n    pumpSends();','close/drop sends observed before and after actual poll; corrupt link withdraws callbacks')
    patch('AdspProbe.cpp','    if ((changed & value & kFatal)) readCrashReason();',
          '    if ((changed & value & kFatal)) readCrashReason();\n    if((value&kFatal)&&gGlinkStarted)qcom_glink_stop(&gGlink);','real fatal state withdraws shared GLINK callbacks; never a DMA stop proof')
    patch('AdspProbe.cpp','for (unsigned i = 0; i < gClientCount; ++i) pending |= gClients[i].count != 0;',
          'for (unsigned i = 0; i < gClientCount; ++i) {IOLockLock(gClientLock);pending |= gClients[i].count != 0 || (gClients[i].owned_status.closing&&!gClients[i].owned_status.fenced);IOLockUnlock(gClientLock);}',
          'actual pending close gets event/backpressure progress; no unchecked queue race')
    # Registration publishes only fully initialized slots; every DSP-side
    # registry traversal observes that release rather than a plain race.
    traversals='i < gClientCount;'
    count=data['AdspProbe.cpp'].count(traversals)
    if count!=2:raise ValueError('Original registry traversal count changed')
    data['AdspProbe.cpp']=data['AdspProbe.cpp'].replace(traversals,'i < __atomic_load_n(&gClientCount,__ATOMIC_ACQUIRE);')
    deltas.append({'file':'AdspProbe.cpp','reason':'two original DSP traversals observe published client count'})
    return data,deltas
