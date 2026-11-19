/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Sole actual GLINK worker calls these. Other threads operate ADSP registry
 * latches only. Closed channel storage is retained as a nonreusable tombstone. */
int elden_glink_close_request(QcomGlink *g,QcomGlinkChannel *c){
    if(!g||!c)return QcomGlinkArgument;int found=0;
    for(unsigned i=0;i<QCOM_GLINK_MAX_CHANNELS;i++)if(c==&g->channel[i]&&c->used)found=1;
    if(!found)return QcomGlinkArgument;if(g->broken)return QcomGlinkCorrupt;
    const int was_closing=c->elden_closing;c->elden_closing=1;
    /* Only an endpoint which NEVER opened has no handshake to await. A peer
     * CLOSE reset open_sent/remote_open after traffic, but is not our ACK. */
    if(!was_closing&&!c->elden_ever_opened&&c->open_sent!=1&&!c->remote_open){c->elden_close_sent=1;c->elden_close_acked=1;c->elden_peer_closed=1;}
    progress(g);
    if(g->kick_pending&&g->kick){g->kick_pending=0;g->kick(g->kick_context);}
    return QcomGlinkOk;
}
int elden_glink_close_status(const QcomGlink *g,const QcomGlinkChannel *c,uint32_t *sent,uint32_t *acked,uint32_t *peer){
    if(!g||!c||!sent||!acked||!peer)return QcomGlinkArgument;
    int found=0;for(unsigned i=0;i<QCOM_GLINK_MAX_CHANNELS;i++)if(c==&g->channel[i]&&c->used)found=1;
    if(!found)return QcomGlinkArgument;
    if(g->broken)return QcomGlinkCorrupt;
    *sent=c->elden_close_sent;*acked=c->elden_close_acked;*peer=c->elden_peer_closed;
    return c->elden_closing&&c->elden_close_sent&&c->elden_close_acked&&c->elden_peer_closed?1:0;
}
