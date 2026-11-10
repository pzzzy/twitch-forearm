#!/usr/bin/env python3
"""Exercise the running ESP32 UI. Injected input is never physical-touch proof."""
import argparse
import json
from pathlib import Path
import time
from device import connected


def check(condition, reason):
    if not condition:
        raise AssertionError(reason)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port')
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    report={'physical_touch_injected':False,'input_method':'explicit LVGL diagnostic injection; controller reads remain separate','checks':{},'samples':[]}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    try:
        with connected(args.port) as d:
            def state(label):
                s=d.command('status');report['samples'].append({'label':label,**s});return s
            initial=state('initial')
            check(initial['board']['touch_present'],'Touch controller did not respond')
            check(initial['board']['flush_errors']==0,'Panel flush failure')
            check(initial['ui']['demo'],'Run UI fixtures before live Twitch login')
            d.command('live');d.command('burst',count=50);time.sleep(.15)
            d.command('pause');d.command('scroll',pixels=-350);before=state('paused-before-arrivals')
            d.command('burst',count=25);time.sleep(.2);after=state('paused-after-arrivals')
            for key in ('anchor_sequence','anchor_offset','scroll_y'):
                check(after['ui'][key]==before['ui'][key],'Paused '+key+' moved on arrival')
            check(after['ui']['paused'],'Paused history resumed unexpectedly')
            check(after['ui']['unread']-before['ui']['unread']==after['ui']['appended']-before['ui']['appended'],'Unread count differs from new arrivals')
            report['checks']['pause_anchor_and_unread']=True
            idle=0
            for index in range(20):
                d.command('pause');before=state('go-live-before-'+str(index))
                d.gesture([(282,445)],step=.075)
                time.sleep(.09);after=state('go-live-after-'+str(index))
                check(not after['ui']['paused'],'Go live did not remain live after one tap at cycle '+str(index))
                if before['ui']['appended']==after['ui']['appended']:idle+=1
            check(idle>=15,'Too few Go live checks without incoming messages')
            report['checks']['go_live_twenty_single_taps']=True
            report['checks']['go_live_without_new_messages_cycles']=idle
            d.command('live');before=state('gesture-before')
            d.gesture([(150,y) for y in range(145,370,12)],step=.035)
            time.sleep(.15);after=state('gesture-after')
            check(after['ui']['paused'],'Drag did not pause live following')
            check(after['ui']['scroll_y']<before['ui']['scroll_y'],'Drag did not scroll back')
            check(not after['ui']['selection_visible'],'Drag opened message selection')
            report['checks']['drag_pauses_and_scrolls_without_selecting']=True
            d.command('live')
            for name,text in [('max-ascii',' '.join(['readable']*62)+' xyz'),('max-emoji','😀'*500),('mixed-unicode','Hello 👋 😀 😂 😭 🔥 💜 👍 ✨ 🌍 🎉 🤔 🥹 ❤️ 🚀 🦄 — café naïve résumé')]:
                if name=='max-ascii':text=(text+' ' * 500)[:500]
                before=state(name+'-before')
                d.command('message',name=name,text=text);time.sleep(.15);after=state(name+'-after')
                check(after['ui']['appended']>before['ui']['appended'],'Fixture not retained: '+name)
                check(after['ui']['rejected']==before['ui']['rejected'],'Fixture rejected: '+name)
                report['checks'][name]={'characters':len(text),'utf8_bytes':len(text.encode()),'accepted':True}
            d.screen(args.output.parent/'native-unicode.png')
            d.command('message',name='emote_fixture',text='Kappa — a real static Twitch emote',emotes=[{'id':'25','text':'Kappa'}])
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                emote_state=state('emote-cache')
                if emote_state.get('emotes',{}).get('ready',0)>0 and emote_state['ui'].get('visible_emote_images',0)>0:break
                time.sleep(.5)
            else:raise AssertionError('Static Twitch emote did not download/decode/render')
            report['checks']['static_twitch_emote']={'ready':emote_state['emotes']['ready'],'downloads':emote_state['emotes']['downloads'],'visible_images':emote_state['ui']['visible_emote_images']}
            d.screen(args.output.parent/'native-emote.png')
            d.command('live');d.command('burst',count=100);time.sleep(.1)
            start=state('scroll-load-start');begun=time.monotonic()
            for count in range(10):
                d.gesture([(150,y) for y in (range(140,371,16) if count%2==0 else range(370,139,-16))],step=.018)
            duration=time.monotonic()-begun;end=state('scroll-load-end')
            report['checks']['scroll_load']={'duration_seconds':duration,'presented_frames':end['board']['frames']-start['board']['frames'],'observed_frames_per_second':(end['board']['frames']-start['board']['frames'])/duration,'ui_frame_p95_us':end.get('ui_frame_p95_us'),'flush_p95_us':end['board']['recent_frame_p95_us']}
            check(end['board']['flush_errors']==0,'Panel errors during scroll load')
            check(end['board']['touch_errors']==0,'I2C errors during scroll load')
            d.command('live')
            d.screen(args.output.parent/'native-chat.png')
            report['final']=state('final')
            report['passed']=True
    except Exception as error:
        report['passed']=False;report['error']=str(error)
        raise
    finally:
        args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'passed':report['passed'],'checks':report['checks']},indent=2))


if __name__=='__main__':main()
