// Halaman dashboard web (disimpan di flash). File ini harus satu folder dengan .ino
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"UIHTML(<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#f5f6fb">
<title>SmartDisplay Dashboard</title>
<style>
:root{--bg:#f5f6fb;--ink:#141a26;--mu:#5d697c;--fa:#97a1b2;--ln:#e6e9f2;--tint:#eef1f8;--ac:#3a56d9;--ac2:#7a6cf0;--acs:#e9edfd;--gd:#0f7a55;--wn:#9a5a06;--wns:#fcefd6;--bd:#bb3326;--ez:cubic-bezier(.2,.8,.2,1);--sf:ui-serif,"New York","Iowan Old Style","Palatino Linotype",Georgia,serif;--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace;color-scheme:light}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;color:var(--ink);font:15px/1.45 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;-webkit-font-smoothing:antialiased;padding-bottom:56px;background:var(--bg);min-height:100vh;overflow-x:hidden}
body::before,body::after{content:"";position:fixed;z-index:-1;width:62vmax;height:62vmax;border-radius:50%;filter:blur(100px);opacity:.55;animation:dr 24s ease-in-out infinite alternate}
body::before{background:#dfe5ff;top:-32vmax;left:-22vmax}
body::after{background:#efe3ff;bottom:-36vmax;right:-24vmax;animation-delay:-10s}
@keyframes dr{to{transform:translate(8vmax,5vmax) scale(1.1)}}
.in{max-width:1040px;margin:0 auto;padding-inline:max(16px,env(safe-area-inset-left,0px))}
svg.i{width:20px;height:20px;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round;flex:none}
button{font:inherit;color:inherit;cursor:pointer}
:focus-visible{outline:2px solid var(--ac);outline-offset:2px}
.top{position:sticky;top:0;z-index:9;padding-top:env(safe-area-inset-top,0px);background:rgba(245,246,251,.74);backdrop-filter:saturate(1.6) blur(16px);-webkit-backdrop-filter:saturate(1.6) blur(16px);border-bottom:1px solid rgba(230,233,242,.9)}
.bar{display:flex;align-items:center;justify-content:space-between;gap:12px;padding-block:12px}
.brand{display:flex;align-items:center;gap:12px;min-width:0}
.mark{width:38px;height:38px;border-radius:12px;background:linear-gradient(145deg,#1b2336,#3a56d9);color:#fff;display:grid;place-items:center;box-shadow:0 10px 20px -8px rgba(58,86,217,.6)}
.brand b{display:block;font-size:16px;font-weight:650;letter-spacing:-.01em}
.brand small{display:block;color:var(--mu);font-size:12px}
.pill{display:inline-flex;align-items:center;gap:8px;padding:6px 13px;border-radius:99px;font-size:13px;font-weight:600;background:#fff;border:1px solid var(--ln);color:var(--mu);white-space:nowrap}
.pill i{width:8px;height:8px;border-radius:50%;background:var(--fa);position:relative}
.pill.ok{color:var(--gd)}.pill.ok i{background:var(--gd)}
.pill.ok i::after{content:"";position:absolute;inset:-4px;border-radius:50%;border:2px solid var(--gd);animation:pg 2s ease-out infinite}
@keyframes pg{from{transform:scale(.4);opacity:.8}to{transform:scale(1.5);opacity:0}}
.pill.demo{color:var(--wn)}.pill.demo i{background:var(--wn)}
.pill.off{color:var(--bd)}.pill.off i{background:var(--bd)}
main{padding-top:22px}
.banner{display:flex;gap:12px;align-items:flex-start;padding:14px 16px;border-radius:16px;background:var(--wns);color:var(--wn);margin-bottom:18px;font-size:14px}
.banner b{display:block;color:var(--ink)}.banner span{color:var(--mu)}
.banner button{margin-left:auto;border:0;background:#fff;border-radius:10px;padding:8px 12px;font-weight:600;color:var(--ink)}
.board{column-width:380px;column-gap:18px}
.sec{break-inside:avoid;display:block;margin:0 0 22px;opacity:0}
body.go .sec{animation:up .9s var(--ez) both;animation-delay:calc(var(--n)*80ms)}
@keyframes up{from{opacity:0;transform:translateY(24px) scale(.985)}to{opacity:1;transform:none}}
.sec>h2{margin:0 0 10px 6px;font-size:12px;font-weight:650;letter-spacing:.09em;text-transform:uppercase;color:var(--mu);display:flex;align-items:center;gap:10px}
.sec>h2::before{content:"";width:18px;height:2px;border-radius:2px;background:linear-gradient(90deg,var(--ac),var(--ac2))}
.panel{background:rgba(255,255,255,.8);backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border-radius:22px;box-shadow:0 0 0 1px rgba(230,233,242,.95),0 1px 2px rgba(20,26,38,.04),0 26px 50px -30px rgba(40,55,130,.35);overflow:hidden;position:relative}
.row{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:14px 18px;min-height:58px;transition:background .3s}
.row+.row,.panel>.row+*{border-top:1px solid var(--ln)}
.row:hover{background:rgba(238,241,248,.45)}
.row .t{min-width:0;flex:1}.row .t b{display:block;font-weight:600}
.row .t small{display:block;color:var(--mu);font-size:13px;line-height:1.35}
.pad{padding:16px 18px}
.hero{padding:22px 22px 20px;background:linear-gradient(150deg,#fff 30%,#eef1ff);isolation:isolate}
.hero::before{content:"";position:absolute;inset:-40% auto -40% -60%;width:40%;background:linear-gradient(100deg,transparent,rgba(255,255,255,.9),transparent);transform:rotate(12deg);animation:sh 8s ease-in-out infinite;z-index:-1}
@keyframes sh{0%,60%{left:-60%}100%{left:130%}}
.hero::after{content:"";position:absolute;right:-40px;top:-40px;width:160px;height:160px;border-radius:50%;background:radial-gradient(circle,rgba(122,108,240,.22),transparent 70%);z-index:-1}
.ht{display:flex;justify-content:space-between;align-items:center;gap:8px}
.eb{font-size:12px;font-weight:650;letter-spacing:.09em;text-transform:uppercase;color:var(--mu)}
.clock{display:flex;align-items:baseline;gap:10px;margin-top:8px;font-variant-numeric:tabular-nums}
#time{display:flex;font:300 clamp(64px,18vw,98px)/1 var(--sf);letter-spacing:-.04em;background:linear-gradient(180deg,#141a26 35%,#4a5fd0);-webkit-background-clip:text;background-clip:text;color:transparent}
#time b,#time i{display:inline-block;font-weight:inherit;font-style:normal}
#time b{min-width:.55em;text-align:center}
#time b.t{animation:rl .55s var(--ez)}
@keyframes rl{from{opacity:0;transform:translateY(.25em);filter:blur(6px)}to{opacity:1;transform:none;filter:none}}
#time i{animation:bk 1s steps(1) infinite;min-width:.3em;text-align:center}
@keyframes bk{50%{opacity:.25}}
#sec{font:500 22px var(--sf);color:var(--ac);opacity:.75;min-width:1.2em}
.date{margin-top:8px;color:var(--mu);font-size:15px}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:18px}
.chip{display:inline-flex;align-items:center;gap:7px;padding:6px 12px;border-radius:99px;background:rgba(255,255,255,.85);box-shadow:0 0 0 1px var(--ln);font-size:13px;font-weight:550}
.chip svg.i{width:16px;height:16px;color:var(--ac)}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px}
.bars i{width:3px;border-radius:1px;background:#d0d6e4;transition:background .4s}
.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:14px}
.bars i.on{background:var(--ac)}
.seg{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;padding:12px}
.seg button{display:flex;flex-direction:column;align-items:center;gap:7px;padding:14px 4px 11px;border:0;border-radius:15px;background:var(--tint);font-size:13px;font-weight:600;color:var(--mu);transition:all .5s var(--ez)}
.seg button svg{transition:transform .5s var(--ez)}
.seg button:hover{transform:translateY(-2px)}
.seg button[aria-pressed=true]{background:linear-gradient(150deg,var(--ac),var(--ac2));color:#fff;box-shadow:0 14px 26px -12px rgba(58,86,217,.75);transform:translateY(-2px)}
.seg button[aria-pressed=true] svg{transform:scale(1.15)}
.seg+.row{border-top:1px solid var(--ln)}
.sw{appearance:none;-webkit-appearance:none;width:48px;height:29px;border-radius:99px;background:#d5dae6;position:relative;cursor:pointer;flex:none;transition:background .35s;margin:0}
.sw::after{content:"";position:absolute;top:3px;left:3px;width:23px;height:23px;border-radius:50%;background:#fff;box-shadow:0 2px 6px rgba(0,0,0,.28);transition:transform .5s cubic-bezier(.3,1.6,.5,1)}
.sw:checked{background:linear-gradient(135deg,var(--ac),var(--ac2))}
.sw:checked::after{transform:translateX(19px)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:30px;background:none;margin:0;cursor:pointer}
input[type=range]::-webkit-slider-runnable-track{height:7px;border-radius:9px;background:linear-gradient(90deg,var(--ac),var(--ac2) var(--p,80%),#dfe3ee var(--p,80%))}
input[type=range]::-moz-range-track{height:7px;border-radius:9px;background:#dfe3ee}
input[type=range]::-moz-range-progress{height:7px;border-radius:9px;background:var(--ac)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:26px;height:26px;margin-top:-9.5px;border-radius:50%;background:#fff;box-shadow:0 0 0 1px var(--ln),0 4px 12px rgba(40,55,150,.4);transition:transform .25s var(--ez)}
input[type=range]::-moz-range-thumb{width:26px;height:26px;border:0;border-radius:50%;background:#fff;box-shadow:0 0 0 1px var(--ln),0 4px 12px rgba(40,55,150,.4)}
input[type=range]:active::-webkit-slider-thumb{transform:scale(1.18)}
.field{width:100%;height:44px;padding:0 14px;border:1px solid var(--ln);border-radius:13px;background:#fafbfe;font:inherit;color:var(--ink);font-variant-numeric:tabular-nums;transition:border-color .25s,box-shadow .25s,background .25s}
.field:focus{outline:0;border-color:var(--ac);background:#fff;box-shadow:0 0 0 4px var(--acs)}
input[type=time].field{width:auto;min-width:120px;font-family:var(--sf);font-size:18px}
.slider{display:flex;align-items:center;gap:14px;padding:6px 18px 18px}
.slider output{min-width:46px;text-align:right;font:600 16px var(--sf)}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px;padding:14px 18px}
.pair label{display:block;font-size:12px;color:var(--mu);margin-bottom:6px;font-weight:600}
.pair .field{min-width:0;width:100%}
.stepper{display:inline-flex;align-items:center;border-radius:13px;overflow:hidden;background:#fafbfe;box-shadow:0 0 0 1px var(--ln)}
.stepper button{width:40px;height:42px;border:0;background:var(--tint);font-size:20px;line-height:1;transition:background .2s,transform .2s}
.stepper button:active{background:var(--acs);transform:scale(.9)}
.stepper input{width:58px;height:42px;border:0;text-align:center;font:600 17px var(--sf);color:var(--ink);background:transparent;-moz-appearance:textfield}
.stepper input::-webkit-outer-spin-button,.stepper input::-webkit-inner-spin-button{-webkit-appearance:none;margin:0}
.btn{position:relative;overflow:hidden;display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:44px;padding:0 18px;border:0;border-radius:13px;background:var(--tint);font-weight:600;white-space:nowrap;transition:transform .25s var(--ez),box-shadow .3s}
.btn:hover{transform:translateY(-1px)}.btn:active{transform:scale(.97)}
.btn.primary{background:linear-gradient(150deg,var(--ac),var(--ac2));color:#fff;box-shadow:0 12px 24px -12px rgba(58,86,217,.75)}
.btn.ink{background:linear-gradient(150deg,#1b2336,#2c3a66);color:#fff;box-shadow:0 12px 24px -12px rgba(20,26,38,.6)}
.btn.block{width:100%}
.btn:disabled{opacity:.45;cursor:default;transform:none}
.btn.spin svg{animation:sp .8s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
.rp{position:absolute;border-radius:50%;background:currentColor;opacity:.22;transform:scale(0);animation:rpl .6s ease-out forwards;pointer-events:none}
@keyframes rpl{to{transform:scale(1);opacity:0}}
.wx{display:flex;align-items:center;gap:16px;padding:18px 18px 6px}
.wx svg.i{width:50px;height:50px;stroke-width:1.4;color:var(--ac);animation:fl 5s ease-in-out infinite}
@keyframes fl{50%{transform:translateY(-5px)}}
.wx .temp{font:300 52px/1 var(--sf);letter-spacing:-.03em}
.wx .d{color:var(--mu);font-size:14px;margin-top:3px}
.wx .upd{margin-left:auto;font-size:12px;color:var(--fa);text-align:right}
.mx{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;padding:12px 18px}
.mx div{background:var(--tint);border-radius:14px;padding:11px 12px}
.mx small{display:block;color:var(--mu);font-size:12px}
.mx b{font:600 18px var(--sf)}
.feature{display:flex;gap:14px;align-items:flex-start;padding:18px;background:linear-gradient(140deg,#eaeeff,#f3ecff)}
.feature .ic{width:42px;height:42px;border-radius:13px;background:linear-gradient(150deg,var(--ac),var(--ac2));color:#fff;display:grid;place-items:center;flex:none;box-shadow:0 10px 20px -8px rgba(58,86,217,.6)}
.feature b{display:block;font-size:16px}.feature p{margin:2px 0 14px;color:var(--mu);font-size:14px}
.gi{width:40px;height:40px;border-radius:12px;background:var(--tint);display:grid;place-items:center;color:var(--ac);flex:none}
.pw{position:relative}
.pw button{position:absolute;right:4px;top:5px;height:34px;border:0;background:transparent;color:var(--mu);font-size:13px;font-weight:600;padding:0 10px}
.msg{min-height:20px;font-size:13px;color:var(--mu)}
.msg.bad{color:var(--bd)}.msg.warn{color:var(--wn)}
dl{margin:0;display:grid;grid-template-columns:auto 1fr}
dt,dd{margin:0;padding:12px 18px;border-top:1px solid var(--ln)}
dt:first-of-type,dt:first-of-type+dd{border-top:0}
dt{color:var(--mu)}dd{text-align:right;font-weight:600;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
dd.mono{font:500 14px var(--mono)}
.foot{margin:6px 6px 0;color:var(--fa);font-size:12px}
.ov{position:fixed;inset:0;background:rgba(20,26,50,.42);backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px);display:grid;place-items:center;padding:16px;z-index:30;opacity:0;pointer-events:none;transition:opacity .35s}
.ov.s{opacity:1;pointer-events:auto}
.dlg{background:#fff;border-radius:26px;padding:24px;max-width:390px;width:100%;box-shadow:0 30px 70px rgba(20,26,50,.35);transform:translateY(24px) scale(.95);transition:transform .55s var(--ez)}
.ov.s .dlg{transform:none}
.dlg h3{margin:0 0 6px;font:600 21px var(--sf)}.dlg p{margin:0 0 20px;color:var(--mu)}
.da{display:flex;gap:10px;justify-content:flex-end}
.toast{position:fixed;left:50%;bottom:calc(20px + env(safe-area-inset-bottom,0px));transform:translate(-50%,30px);opacity:0;background:var(--ink);color:#fff;padding:12px 18px;border-radius:14px;font-size:14px;font-weight:550;z-index:40;max-width:calc(100% - 32px);box-shadow:0 18px 40px -12px rgba(20,26,50,.5);transition:transform .5s var(--ez),opacity .4s;pointer-events:none}
.toast.s{transform:translate(-50%,0);opacity:1}
.toast.bad{background:var(--bd)}
.chk{display:flex;flex-wrap:wrap;gap:8px;margin:0 0 10px}
.chk label{display:inline-flex;align-items:center;gap:8px;padding:8px 12px;border-radius:99px;background:var(--tint);font-size:13px;font-weight:600;color:var(--mu);cursor:pointer;transition:all .3s var(--ez)}
.chk label:has(input:checked){background:var(--acs);color:var(--ac);box-shadow:0 0 0 1px var(--ac)}
.chk input{accent-color:var(--ac);width:16px;height:16px;margin:0}
.fc{padding:2px 18px 8px}
.fcr{display:grid;grid-template-columns:62px 1fr auto 44px;gap:10px;align-items:center;padding:10px 0;border-top:1px solid var(--ln);font-size:14px}
.fcr:first-child{border-top:0}
.fcr small{display:block;color:var(--mu);font-size:12px}
.fcr b{font-weight:600}.fcr .tt{font-variant-numeric:tabular-nums;font-weight:600}.fcr .pp{text-align:right;color:var(--ac);font-weight:600}
.fcr.now{margin:0 -10px;padding-inline:10px;border-radius:12px;background:var(--acs);border-top-color:transparent}
.mx small+small{margin-top:2px;color:var(--fa)}
.tag{display:inline-block;margin-left:8px;padding:2px 8px;border-radius:99px;background:var(--acs);color:var(--ac);font-size:11px;font-weight:650;vertical-align:middle}
.tag.ap{background:var(--wns);color:var(--wn)}
.gp{font:300 44px/1.1 var(--sf);letter-spacing:-.02em}
.up{color:var(--gd)}.dn{color:var(--bd)}
.spark{display:block;width:100%;height:76px;padding:0 18px 8px}
.wl .row{min-height:54px}
.wl .row .btn{min-height:36px;padding:0 12px;font-size:13px}
.wl .row .bt{display:flex;gap:8px}
.pt .t small{font-variant-numeric:tabular-nums;font-size:15px;color:var(--ink);font-weight:550}
@media(max-width:420px){.seg{gap:6px;padding:10px}.row{padding:12px 14px}}
@media(prefers-reduced-motion:reduce){*,*::before,*::after{animation-duration:.01ms!important;animation-iteration-count:1!important;transition-duration:.01ms!important}.sec{opacity:1}}
</style>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true">
<symbol id="i-clock" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></symbol>
<symbol id="i-cloud" viewBox="0 0 24 24"><path d="M7 18a4 4 0 0 1-.5-7.97A5.5 5.5 0 0 1 17.2 9.5 4.25 4.25 0 0 1 17 18z"/></symbol>
<symbol id="i-rain" viewBox="0 0 24 24"><path d="M7 15a4 4 0 0 1-.5-7.97A5.5 5.5 0 0 1 17.2 6.5 4.25 4.25 0 0 1 17 15z"/><path d="M8 18l-1 3M12 18l-1 3M16 18l-1 3"/></symbol>
<symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></symbol>
<symbol id="i-moon" viewBox="0 0 24 24"><path d="M21 13A9 9 0 1 1 11 3a7 7 0 0 0 10 10z"/></symbol>
<symbol id="i-face" viewBox="0 0 24 24"><circle cx="8.5" cy="10" r="2.2"/><circle cx="15.5" cy="10" r="2.2"/><path d="M9 16c1.7 1.4 4.3 1.4 6 0"/></symbol>
<symbol id="i-info" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/></symbol>
<symbol id="i-nav" viewBox="0 0 24 24"><path d="M3 11l18-8-8 18-2-8z"/></symbol>
<symbol id="i-spk" viewBox="0 0 24 24"><path d="M11 5L6 9H3v6h3l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/></symbol>
<symbol id="i-coin" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M9.2 9.6c0-1.2 1.2-2 2.8-2s2.8.8 2.8 2-1.2 1.6-2.8 2-2.8.8-2.8 2 1.2 2 2.8 2 2.8-.8 2.8-2M12 6v1.6M12 16.4V18"/></symbol>
<symbol id="i-rf" viewBox="0 0 24 24"><path d="M20 11a8 8 0 1 0-2.3 6.3M20 4v7h-7"/></symbol>
</svg>

<header class="top"><div class="in bar">
<div class="brand"><div class="mark"><svg class="i"><use href="#i-face"/></svg></div><div><b>SmartDisplay</b><small>ESP32 &middot; MotoNav</small></div></div>
<span class="pill" id="conn"><i></i><span id="connTxt">Menghubungkan</span></span>
</div></header>

<main class="in">
<div class="banner" id="banner" hidden><svg class="i"><use href="#i-moon"/></svg><div><b id="banTitle"></b><span id="banText"></span></div><button type="button" id="banBtn" hidden>Keluar (simulasi)</button></div>
<div class="board">

<section class="sec"><div class="panel hero">
<div class="ht"><span class="eb">Waktu perangkat</span><span class="chip"><span class="bars" id="bars"><i></i><i></i><i></i><i></i></span><span id="rssiTxt">--</span></span></div>
<div class="clock"><span id="time"><b>-</b><b>-</b><i>:</i><b>-</b><b>-</b></span><span id="sec">--</span></div>
<div class="date" id="date">--</div>
<div class="chips"><span class="chip"><svg class="i"><use href="#i-face"/></svg><span id="chipMode">Tampilan --</span></span><span class="chip"><svg class="i"><use href="#i-sun"/></svg><span id="chipOled">OLED --</span></span></div>
</div></section>

<section class="sec"><h2>Tampilan layar</h2><div class="panel">
<div class="seg" id="seg">
<button type="button" data-m="0" aria-pressed="false"><svg class="i"><use href="#i-clock"/></svg>Jam</button>
<button type="button" data-m="1" aria-pressed="false"><svg class="i"><use href="#i-cloud"/></svg>Cuaca</button>
<button type="button" data-m="2" aria-pressed="false"><svg class="i"><use href="#i-face"/></svg>Mata</button>
<button type="button" data-m="3" aria-pressed="false"><svg class="i"><use href="#i-info"/></svg>Info</button>
<button type="button" data-m="4" aria-pressed="false"><svg class="i"><use href="#i-sun"/></svg>Prakiraan</button>
<button type="button" data-m="5" aria-pressed="false"><svg class="i"><use href="#i-moon"/></svg>Sholat</button>
<button type="button" data-m="6" aria-pressed="false"><svg class="i"><use href="#i-coin"/></svg>Emas</button>
</div>
<div class="row"><div class="t"><b>Ganti otomatis</b><small>Pindah tampilan tiap 8 detik</small></div><input class="sw" type="checkbox" id="autoMode" role="switch" aria-label="Ganti tampilan otomatis"></div>
<div class="pad" style="border-top:1px solid var(--ln)"><div class="eb" style="margin-bottom:10px">Halaman yang ikut berganti otomatis</div><div class="chk" id="autoMask"></div><small style="color:var(--fa)">Hilangkan centang untuk menyembunyikan halaman dari putaran otomatis. Bila semua dikosongkan, layar tidak berganti sendiri.</small></div>
</div></section>

<section class="sec"><h2>Cuaca</h2><div class="panel">
<div class="wx"><svg class="i" id="wxIcon"><use href="#i-cloud"/></svg><div><div class="temp" id="temp">--&deg;</div><div class="d" id="desc">--</div></div><div class="upd">Diperbarui<br><b id="last" style="color:var(--ink)">--</b></div></div>
<div class="mx"><div><small>Kelembapan</small><b id="hum">--</b></div><div><small>Terasa</small><b id="feels">--</b></div><div><small>Angin</small><b id="wind">--</b></div></div>
<div class="mx" style="padding-top:0"><div><small>Indeks UV</small><b id="uvV">--</b><small id="uvW">&nbsp;</small></div><div><small>Kualitas udara</small><b id="aqiV">--</b><small id="aqiW">&nbsp;</small></div><div><small>Peluang hujan</small><b id="popV">--</b><small>hari ini</small></div></div>
<div class="fc" id="fcList"></div>
<div class="pad" style="padding-top:4px"><button type="button" class="btn block" id="wxBtn"><svg class="i"><use href="#i-rf"/></svg>Perbarui cuaca &amp; prakiraan</button><small id="fcInfo" style="display:block;margin-top:8px;color:var(--fa)"></small></div>
</div></section>

<section class="sec"><h2>Waktu sholat</h2><div class="panel">
<div class="row"><div class="t"><b>Pengingat otomatis</b><small>Layar menampilkan popup + buzzer saat masuk waktu. Klik tombol untuk menutup.</small></div><input class="sw" type="checkbox" id="prOn" role="switch" aria-label="Pengingat sholat"></div>
<div id="prList"></div>
<div class="pad"><button type="button" class="btn block" id="prBtn"><svg class="i"><use href="#i-rf"/></svg>Perbarui jadwal</button><small id="prInfo" style="display:block;margin-top:8px;color:var(--fa)">Metode Kemenag RI, mengikuti lokasi cuaca.</small></div>
</div></section>

<section class="sec"><h2>Harga emas</h2><div class="panel">
<div class="pad" style="text-align:center;padding-bottom:6px"><div class="eb">Per 1 gram</div><div class="gp" id="gPrice">Rp --</div><div id="gChg" style="font-weight:600;min-height:22px"></div></div>
<svg class="spark" id="gSpark" viewBox="0 0 300 70" preserveAspectRatio="none" aria-hidden="true"></svg>
<div class="mx"><div><small>Spot dunia</small><b id="gXau">--</b></div><div><small>Kurs USD</small><b id="gFx">--</b></div><div><small>Diperbarui</small><b id="gLast">--</b></div></div>
<div class="row"><div class="t"><b>Penyesuaian harga</b><small>Persen tambah/kurang agar mendekati harga toko (mis. Antam)</small></div><div class="stepper"><button type="button" data-g="-0.5" aria-label="Kurangi">&minus;</button><input id="gAdj" type="number" min="-50" max="50" step="0.5" value="0" inputmode="decimal"><button type="button" data-g="0.5" aria-label="Tambah">+</button></div></div>
<div class="pad" style="display:grid;grid-template-columns:1fr 1fr;gap:10px"><button type="button" class="btn primary" id="gSave" disabled>Simpan</button><button type="button" class="btn" id="gBtn"><svg class="i"><use href="#i-rf"/></svg>Perbarui harga</button></div>
<div class="pad" style="padding-top:0"><small style="color:var(--fa)">Harga dihitung dari emas spot dunia &times; kurs USD/IDR, jadi biasanya di bawah harga jual toko.</small></div>
</div></section>

<section class="sec"><h2>Pelacak jejak</h2><div class="panel">
<div class="pad"><small style="color:var(--fa)">GPS HP dikirim lewat Bluetooth oleh aplikasi &quot;Bluetooth GPS Output&quot; (Play Store). Jejak digambar di OLED saat mode NAV (2x klik tombol untuk membuka layar jejak). Setelah selesai, jejak dijadikan PNG dan diunggah ke GitHub begitu ESP32 tersambung WiFi.</small></div>
<div class="pad" style="display:grid;gap:10px;padding-top:0">
<input class="field" id="ghPhone" type="text" placeholder="HP: alamat Bluetooth (AA:BB:CC:DD:EE:FF) atau nama" autocomplete="off" autocapitalize="off" aria-label="Alamat atau nama Bluetooth HP">
<small style="color:var(--fa);margin-top:-4px">Alamat ada di Pengaturan HP &rarr; Tentang ponsel &rarr; Status &rarr; Alamat Bluetooth. Pakai alamat agar HP tidak perlu dalam mode terlihat.</small>
<input class="field" id="ghOwner" type="text" placeholder="Pemilik repo (username GitHub)" autocomplete="off" autocapitalize="off" aria-label="Pemilik repo">
<input class="field" id="ghRepo" type="text" placeholder="Nama repo" autocomplete="off" autocapitalize="off" aria-label="Nama repo">
<div style="display:grid;grid-template-columns:1fr 1fr;gap:10px"><input class="field" id="ghBranch" type="text" placeholder="Branch (main)" autocomplete="off" autocapitalize="off" aria-label="Branch"><input class="field" id="ghDir" type="text" placeholder="Folder (tracks)" autocomplete="off" autocapitalize="off" aria-label="Folder"></div>
<input class="field" id="ghToken" type="password" placeholder="Token GitHub" autocomplete="new-password" aria-label="Token GitHub">
<small id="ghTokInfo" style="color:var(--fa)">Token: fine-grained, izin Contents &rarr; Read and write hanya untuk repo ini.</small>
<div style="display:grid;grid-template-columns:1fr 1fr;gap:10px"><button type="button" class="btn primary" id="ghSave">Simpan</button><button type="button" class="btn" id="trkUp">Unggah sekarang</button></div>
</div>
<div class="pad" style="padding-top:0"><small id="trkInfo" style="color:var(--fa)">--</small><div id="trkFiles"></div></div>
</div></section>

<section class="sec"><h2>Animasi mata</h2><div class="panel">
<div class="row"><div class="t"><b>Jeda minimal</b><small>Detik sebelum animasi berikutnya</small></div><div class="stepper"><button type="button" data-s="eyeMin" data-d="-1" aria-label="Kurangi">&minus;</button><input id="eyeMin" type="number" min="1" max="600" value="4" inputmode="numeric"><button type="button" data-s="eyeMin" data-d="1" aria-label="Tambah">+</button></div></div>
<div class="row"><div class="t"><b>Jeda maksimal</b><small>Waktu tunggu dipilih acak di antaranya</small></div><div class="stepper"><button type="button" data-s="eyeMax" data-d="-1" aria-label="Kurangi">&minus;</button><input id="eyeMax" type="number" min="1" max="600" value="20" inputmode="numeric"><button type="button" data-s="eyeMax" data-d="1" aria-label="Tambah">+</button></div></div>
<div class="row"><div class="t"><small id="eyeHint">Animasi acak tiap 4&ndash;20 detik</small></div><button type="button" class="btn primary" id="eyeSave" disabled>Simpan</button></div>
</div></section>

<section class="sec"><h2>Kecerahan</h2><div class="panel">
<div class="row"><div class="t"><b>Otomatis</b><small>Terang siang hari, redup malam hari</small></div><input class="sw" type="checkbox" id="autoBright" role="switch" aria-label="Kecerahan otomatis"></div>
<div class="slider"><svg class="i" style="color:var(--fa)"><use href="#i-sun"/></svg><input id="bright" type="range" min="0" max="255" value="220" aria-label="Kecerahan manual"><output id="brightOut">86%</output></div>
</div></section>

<section class="sec"><h2>Alarm harian</h2><div class="panel">
<div class="row"><div class="t"><b>Alarm aktif</b><small>Buzzer berbunyi 5 kali</small></div><input class="sw" type="checkbox" id="alarmEn" role="switch" aria-label="Alarm aktif"></div>
<div class="row"><div class="t"><b>Jam alarm</b></div><input class="field" type="time" id="alarmTime" value="07:00" aria-label="Jam alarm"></div>
</div></section>

<section class="sec"><h2>Jadwal layar tidur</h2><div class="panel">
<div class="row"><div class="t"><b>Matikan layar terjadwal</b><small>Layar mati otomatis di rentang jam ini</small></div><input class="sw" type="checkbox" id="sleepEn" role="switch" aria-label="Jadwal layar tidur"></div>
<div class="pair"><div><label for="sleepStart">Mulai tidur</label><input class="field" type="time" id="sleepStart" value="22:00"></div><div><label for="sleepEnd">Bangun</label><input class="field" type="time" id="sleepEnd" value="06:00"></div></div>
</div></section>

<section class="sec"><h2>Mode &amp; aksi</h2>
<div class="panel feature" style="margin-bottom:14px"><div class="ic"><svg class="i"><use href="#i-nav"/></svg></div><div style="min-width:0"><b>MotoNav</b><p>Pindah ke navigasi lewat Bluetooth. Dashboard ini terputus selama mode navigasi.</p><button type="button" class="btn primary" id="navBtn">Mulai MotoNav</button></div></div>
<div class="panel">
<div class="row"><div class="gi"><svg class="i"><use href="#i-spk"/></svg></div><div class="t"><b>Tes buzzer</b><small>Bunyikan sebentar</small></div><button type="button" class="btn" id="beepBtn">Bunyikan</button></div>
<div class="row"><div class="gi"><svg class="i"><use href="#i-moon"/></svg></div><div class="t"><b>Deep Sleep</b><small>Matikan WiFi dan web. Keluar: klik tombol GPIO 4 tiga kali cepat.</small></div><button type="button" class="btn" id="dsBtn">Aktifkan</button></div>
</div></section>

<section class="sec"><h2>Jaringan WiFi</h2><div class="panel wl">
<div class="banner" id="apBan" hidden style="margin:14px;border-radius:14px"><svg class="i"><use href="#i-info"/></svg><div><b>AP darurat aktif</b><span id="apTxt"></span></div></div>
<div id="wlList"></div>
<div class="pad" style="border-top:1px solid var(--ln)"><div style="display:grid;gap:10px">
<input class="field" id="wifiSSID" type="text" placeholder="Nama WiFi (SSID)" autocomplete="off" autocapitalize="off" aria-label="Nama WiFi">
<div class="pw"><input class="field" id="wifiPASS" type="password" placeholder="Kata sandi" autocomplete="off" aria-label="Kata sandi WiFi"><button type="button" id="pwToggle">Tampilkan</button></div>
<button type="button" class="btn ink block" id="wifiBtn">Tambah &amp; sambungkan</button>
<div class="msg" id="wifiMsg" role="status"></div>
<small style="color:var(--fa)">Maksimal 5 WiFi tersimpan; ESP32 memilih yang sinyalnya terkuat. Jika semuanya gagal, hotspot darurat menyala. Bisa juga lewat Serial Monitor: <span style="font-family:var(--mono)">WIFI:nama:sandi</span></small>
</div></div></div></section>

<section class="sec"><h2>Info perangkat</h2><div class="panel"><dl>
<dt>Alamat IP</dt><dd class="mono" id="ip">--</dd><dt>Sinyal WiFi</dt><dd id="rssi">--</dd><dt>Status</dt><dd id="status">--</dd><dt>Mode sistem</dt><dd id="sysmode">--</dd><dt>Layar OLED</dt><dd id="oled">--</dd>
</dl></div><p class="foot" id="foot"></p></section>

</div></main>

<div class="ov" id="modal"><div class="dlg" role="dialog" aria-modal="true" aria-labelledby="mTitle"><h3 id="mTitle"></h3><p id="mText"></p><div class="da"><button type="button" class="btn" id="mNo">Batal</button><button type="button" class="btn primary" id="mYes">Lanjut</button></div></div></div>
<div class="toast" id="toast" role="status"></div>

<script>
(function(){
'use strict';
var $=function(i){return document.getElementById(i)},M=['Jam','Cuaca','Mata','Info','Prakiraan','Sholat','Emas'],D=['Minggu','Senin','Selasa','Rabu','Kamis','Jumat','Sabtu'],N=['Jan','Feb','Mar','Apr','Mei','Jun','Jul','Agu','Sep','Okt','Nov','Des'];
var demo=false,halted=false,fails=0,timer=null,dirty=false,lastW=null,lastF=null,prSig='',wlSig='',gDirty=false;
var S={mode:2,auto:true,sleep:false,temp:29.4,humidity:78,feels:33.1,wind:11.2,desc:'Hujan ringan',last:'09:30',ip:'192.168.18.51',rssi:-58,alarm_en:true,alarm:'05:30',autoBright:true,brightness:220,systemMode:'WIFI_MODE',wifi:true,sleepWindow:'22:00-06:00',sleepEn:true,eyeMin:4,eyeMax:20};
var F={autoMask:127,aqi:62,pm25:18,uv:7.2,fcValid:true,fcLast:'09:30',fc:[{d:'Kamis',dm:'29/10',c:'Hujan',hi:31,lo:24,p:80,uv:7.1},{d:'Jumat',dm:'30/10',c:'Berawan',hi:32,lo:24,p:40,uv:8},{d:'Sabtu',dm:'31/10',c:'Hujan',hi:30,lo:23,p:70,uv:6.4},{d:'Minggu',dm:'01/11',c:'Cerah',hi:33,lo:24,p:10,uv:9.1},{d:'Senin',dm:'02/11',c:'Mendung',hi:31,lo:24,p:50,uv:7}],pr:{on:true,mask:31,valid:true,next:2,t:['04:30','05:42','11:47','15:07','17:51','19:01']},gold:{valid:true,gram:2345678,open:2331000,xau:4120.5,fx:16420,adj:0,last:'09:30',h:[2331000,2333500,2330200,2336800,2340100,2338000,2342500,2345678]}};
var W={list:['Rumah','Kantor'],cur:'Rumah',conn:true,ap:false,apName:'SmartDisplay-Setup',apPass:'smartdisplay',busy:false,res:'',max:5};
function p2(n){return(n<10?'0':'')+n}
function st(){var d=new Date();S.time=p2(d.getHours())+':'+p2(d.getMinutes())+':'+p2(d.getSeconds());S.date=D[d.getDay()]+', '+p2(d.getDate())+' '+N[d.getMonth()]+' '+d.getFullYear();return S}
function mock(path){var u=new URL(path,'http://x'),q=u.searchParams,r=u.pathname,b='OK';
if(r=='/status')b=JSON.stringify(st());
else if(r=='/mode'){S.mode=+q.get('val');S.auto=false}
else if(r=='/auto')S.auto=q.get('val')=='1';
else if(r=='/weather'){S.temp=+(S.temp+Math.random()-.5).toFixed(1);S.last=st().time.slice(0,5)}
else if(r=='/brightness'){S.autoBright=false;S.brightness=+q.get('val')}
else if(r=='/autobright')S.autoBright=q.get('val')=='1';
else if(r=='/alarm'){S.alarm=p2(+q.get('hour'))+':'+p2(+q.get('min'));S.alarm_en=q.get('en')=='1'}
else if(r=='/sleep'){S.sleepWindow=q.get('start')+'-'+q.get('end');S.sleepEn=q.get('en')=='1'}
else if(r=='/eyes'){S.eyeMin=+q.get('min');S.eyeMax=Math.max(+q.get('min'),+q.get('max'))}
else if(r=='/feat')b=JSON.stringify(F);
else if(r=='/automask')F.autoMask=+q.get('val');
else if(r=='/prayer'){if(q.get('on')!=null)F.pr.on=q.get('on')=='1';if(q.get('mask')!=null)F.pr.mask=+q.get('mask')}
else if(r=='/gold'){if(q.get('adj')!=null)F.gold.adj=+q.get('adj')}
else if(r=='/wifis')b=JSON.stringify(W);
else if(r=='/wifidel')W.list.splice(+q.get('i'),1);
else if(r=='/wifiuse')b='TRYING:'+W.list[+q.get('i')];
else if(r=='/setwifi'){b='TRYING:'+q.get('ssid');if(W.list.indexOf(q.get('ssid'))<0)W.list.unshift(q.get('ssid'))}
return Promise.resolve({ok:true,text:function(){return Promise.resolve(b)},json:function(){return Promise.resolve(JSON.parse(b))}})}
function get(p,ms){if(demo)return mock(p);var c=new AbortController(),t=setTimeout(function(){c.abort()},ms||4000);return fetch(p,{cache:'no-store',signal:c.signal}).then(function(r){clearTimeout(t);return r},function(e){clearTimeout(t);throw e})}
function call(p,m){return get(p).then(function(r){if(!r.ok)throw 0;if(m)toast(m);return poll().then(function(){return r})}).catch(function(){toast('Perangkat tidak merespons','bad')})}
function poll(){return get('/status').then(function(r){if(!r.ok)throw 0;return r.json()}).then(function(j){fails=0;conn(demo?'demo':'ok');render(j)}).catch(function(){fails++;if(fails>=3)conn('off')})}
function conn(k){$('conn').className='pill '+(k=='ok'||k=='demo'||k=='off'?k:'');$('connTxt').textContent=k=='ok'?'Terhubung':k=='demo'?'Pratinjau':k=='off'?'Terputus':'Menghubungkan'}
function toast(m,k){var t=$('toast');t.textContent=m;t.className='toast s'+(k=='bad'?' bad':'');clearTimeout(toast.h);toast.h=setTimeout(function(){t.classList.remove('s')},2600)}
function ask(t,x,ok){return new Promise(function(res){$('mTitle').textContent=t;$('mText').textContent=x;$('mYes').textContent=ok||'Lanjut';$('modal').classList.add('s');$('mYes').focus();function d(v){$('modal').classList.remove('s');$('mYes').onclick=$('mNo').onclick=null;res(v)}$('mYes').onclick=function(){d(true)};$('mNo').onclick=function(){d(false)}})}
function sv(el,v){if(document.activeElement!==el&&el.value!==String(v))el.value=v}
function sc(el,v){if(document.activeElement!==el)el.checked=!!v}
function bars(r){return r<=-900?0:r>-55?4:r>-67?3:r>-78?2:1}
function word(r){return r<=-900?'Tidak ada':r>-55?'Sangat baik':r>-67?'Baik':r>-78?'Cukup':'Lemah'}
function pr(){var r=$('bright'),p=Math.round(r.value/2.55);r.style.setProperty('--p',p+'%');$('brightOut').textContent=p+'%'}
function clk(t){var d=t.slice(0,5).replace(':',''),c=$('time').children,ix=[0,1,3,4];for(var i=0;i<4;i++){var e=c[ix[i]];if(e.textContent!==d[i]){e.textContent=d[i];e.classList.remove('t');void e.offsetWidth;e.classList.add('t')}}}
function render(j){var t=j.time||'00:00:00';clk(t);$('sec').textContent=t.slice(6,8)||'--';$('date').textContent=j.date||'--';
var m=j.mode|0;$('chipMode').textContent='Tampilan '+M[m]+(j.auto?' (otomatis)':'');$('chipOled').textContent=j.sleep?'OLED tidur':'OLED menyala';
var rs=typeof j.rssi=='number'?j.rssi:-999,nb=bars(rs);Array.prototype.forEach.call($('bars').children,function(b,i){b.className=i<nb?'on':''});
$('rssiTxt').textContent=rs<=-900?'Offline':rs+' dBm';
Array.prototype.forEach.call($('seg').children,function(b){b.setAttribute('aria-pressed',+b.getAttribute('data-m')===m?'true':'false')});
sc($('autoMode'),j.auto);
$('temp').textContent=(j.temp!=null?Math.round(j.temp):'--')+'°';var ds=j.desc||'--';$('desc').textContent=ds;var lo=ds.toLowerCase(),ic=/hujan|rain|gerimis|petir|badai/.test(lo)?'i-rain':/cerah|sun|clear|terang/.test(lo)?'i-sun':'i-cloud';
$('wxIcon').innerHTML='<use href="#'+ic+'"/>';$('last').textContent=j.last||'--';
$('hum').textContent=(j.humidity!=null?Math.round(j.humidity):'--')+'%';$('feels').textContent=(j.feels!=null?Math.round(j.feels):'--')+'°C';$('wind').textContent=(j.wind!=null?Math.round(j.wind):'--')+' km/j';
sc($('autoBright'),j.autoBright);if(typeof j.brightness=='number'){sv($('bright'),j.brightness);pr()}
sc($('alarmEn'),j.alarm_en);sv($('alarmTime'),j.alarm||'07:00');
var w=String(j.sleepWindow||'22:00-06:00').split('-');sv($('sleepStart'),w[0]);sv($('sleepEnd'),w[1]);if(typeof j.sleepEn!='undefined')sc($('sleepEn'),j.sleepEn);
if(!dirty&&typeof j.eyeMin=='number'){sv($('eyeMin'),j.eyeMin);sv($('eyeMax'),j.eyeMax);eh()}
$('ip').textContent=j.ip||'--';$('rssi').textContent=rs<=-900?'--':rs+' dBm · '+word(rs);$('status').textContent=j.wifi?'WiFi terhubung':'Offline';$('sysmode').textContent=j.systemMode||'--';$('oled').textContent=j.sleep?'Tidur':'Menyala'}
function nf(n){return String(Math.round(n)).replace(/\B(?=(\d{3})+(?!\d))/g,'.')}
function esc(t){return String(t).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function uvw(u){return u<0?'':u<3?'Rendah':u<6?'Sedang':u<8?'Tinggi':u<11?'Sangat tinggi':'Ekstrem'}
function aqw(a){return a<0?'':a<=50?'Baik':a<=100?'Sedang':a<=150?'Kurang sehat':a<=200?'Tidak sehat':a<=300?'Sangat tidak sehat':'Berbahaya'}
var PN=['Subuh','Terbit','Dzuhur','Ashar','Maghrib','Isya'],PK={0:0,2:1,3:2,4:3,5:4};
function chips(mask){var box=$('autoMask');if(!box.children.length){box.innerHTML=M.map(function(n,i){return'<label><input type="checkbox" data-b="'+i+'"><span>'+n+'</span></label>'}).join('')}
Array.prototype.forEach.call(box.querySelectorAll('input'),function(c){if(document.activeElement!==c)c.checked=!!(mask&(1<<(+c.getAttribute('data-b'))))})}
function renderFeat(j){lastF=j;chips(j.autoMask|0);
var u=typeof j.uv=='number'?j.uv:-1,a=typeof j.aqi=='number'?j.aqi:-1;
$('uvV').textContent=u<0?'--':Math.round(u);$('uvW').innerHTML=esc(uvw(u))||'&nbsp;';
$('aqiV').textContent=a<0?'--':a;$('aqiW').innerHTML=esc(aqw(a))||'&nbsp;';
var fc=j.fc||[];$('popV').textContent=fc.length?fc[0].p+'%':'--';
$('fcList').innerHTML=j.fcValid&&fc.length?fc.map(function(f,i){return'<div class="fcr'+(i==0?' now':'')+'"><div><b>'+esc(f.d)+'</b><small>'+esc(f.dm)+'</small></div><div>'+esc(f.c)+'<small>UV '+Math.round(f.uv)+'</small></div><div class="tt">'+f.lo+'°/'+f.hi+'°</div><div class="pp">'+f.p+'%</div></div>'}).join(''):'<div class="msg" style="padding:12px 0">Belum ada data prakiraan. Tekan tombol perbarui di bawah.</div>';
$('fcInfo').textContent=j.fcValid?'Prakiraan diperbarui '+(j.fcLast||'--')+' · otomatis tiap 1 jam':'';
var pr=j.pr||{};sc($('prOn'),pr.on);
var sig=JSON.stringify([pr.t,pr.mask,pr.next,pr.valid]);
if(sig!==prSig&&!$('prList').contains(document.activeElement)){prSig=sig;
$('prList').innerHTML=pr.valid?(pr.t||[]).map(function(t,i){var k=PK[i];var nx=(k!==undefined&&k===pr.next);
return'<div class="row pt"><div class="t"><b>'+PN[i]+(nx?'<span class="tag">Berikutnya</span>':'')+'</b><small>'+t+'</small></div>'+(k===undefined?'':'<input class="sw" type="checkbox" data-k="'+k+'" aria-label="Ingatkan '+PN[i]+'"'+((pr.mask>>k)&1?' checked':'')+'>')+'</div>'}).join(''):'<div class="row"><div class="t"><small>Belum ada jadwal. Tekan "Perbarui jadwal" saat WiFi tersambung.</small></div></div>'}
var g=j.gold||{};
if(g.valid){$('gPrice').textContent='Rp '+nf(g.gram);var d=g.open>0?(g.gram-g.open)/g.open*100:0;
$('gChg').className=d>0.004?'up':d<-0.004?'dn':'';$('gChg').textContent=(d>0.004?'▲ +':d<-0.004?'▼ −':'')+(Math.abs(d)<0.005?'Stabil hari ini':Math.abs(d).toFixed(2)+'% hari ini');
$('gXau').textContent=g.xau>0?'$'+nf(g.xau)+'/oz':'--';$('gFx').textContent=g.fx>0?'Rp '+nf(g.fx):'--';$('gLast').textContent=g.last||'--';
var h=g.h||[];if(h.length>1){var mn=Math.min.apply(0,h),mx=Math.max.apply(0,h),sp=mx-mn||1,pts=h.map(function(v,i){return(i*300/(h.length-1)).toFixed(1)+','+(64-(v-mn)/sp*58).toFixed(1)}).join(' ');
$('gSpark').innerHTML='<polyline fill="none" stroke="url(#gg)" stroke-width="2.5" stroke-linejoin="round" stroke-linecap="round" vector-effect="non-scaling-stroke" points="'+pts+'"/><defs><linearGradient id="gg" x1="0" x2="1"><stop offset="0" stop-color="#3a56d9"/><stop offset="1" stop-color="#7a6cf0"/></linearGradient></defs>'}
else $('gSpark').innerHTML='<text x="150" y="40" text-anchor="middle" fill="#97a1b2" font-size="12">Grafik terkumpul seiring waktu</text>'}
else{$('gPrice').textContent='Rp --';$('gChg').textContent='';$('gSpark').innerHTML='<text x="150" y="40" text-anchor="middle" fill="#97a1b2" font-size="12">Belum ada data harga</text>'}
if(!gDirty&&typeof g.adj=='number')sv($('gAdj'),g.adj)}
function loadFeat(){if(halted)return Promise.resolve();return get('/feat').then(function(r){if(!r.ok)throw 0;return r.json()}).then(renderFeat).catch(function(){})}
function renderWifis(w){lastW=w;var sig=JSON.stringify([w.list,w.cur,w.ap,w.busy]);
$('apBan').hidden=!w.ap;if(w.ap)$('apTxt').textContent='Sambungkan HP ke "'+w.apName+'" (sandi: '+w.apPass+') lalu buka 192.168.4.1';
if(sig===wlSig)return;wlSig=sig;
$('wlList').innerHTML=(w.list&&w.list.length)?w.list.map(function(n,i){var cur=w.conn&&n===w.cur;
return'<div class="row"><div class="t"><b>'+esc(n)+(cur?'<span class="tag">Tersambung</span>':'')+'</b><small>Prioritas '+(i+1)+'</small></div><div class="bt">'+(cur?'':'<button type="button" class="btn" data-use="'+i+'">Pakai</button>')+'<button type="button" class="btn" data-del="'+i+'">Hapus</button></div></div>'}).join(''):'<div class="row"><div class="t"><small>Belum ada WiFi tersimpan.</small></div></div>'}
function loadWifis(){if(halted)return Promise.resolve();return get('/wifis').then(function(r){if(!r.ok)throw 0;return r.json()}).then(renderWifis).catch(function(){})}
function waitWifi(name){var m=$('wifiMsg'),n=0;
if(demo){setTimeout(function(){m.className='msg';m.textContent='Tersambung ke "'+name+'" (pratinjau).';wlSig='';loadWifis()},1500);return}
if(!(lastW&&lastW.ap)){m.textContent+=' Halaman dimuat ulang otomatis.';setTimeout(function(){location.reload()},22000);return}
var iv=setInterval(function(){n++;get('/wifis').then(function(r){return r.json()}).then(function(w){if(!w.busy&&w.res){clearInterval(iv);renderWifis(w);
if(w.res.indexOf('ok:')==0){m.className='msg';m.textContent='Tersambung ke "'+w.res.slice(3)+'". AP darurat dimatikan: sambungkan HP ke WiFi itu, lalu buka alamat IP baru yang tampil di layar OLED.'}
else{m.className='msg bad';m.textContent='Gagal tersambung ke "'+w.res.slice(5)+'". Periksa nama dan sandi, lalu coba lagi.'}}}).catch(function(){});
if(n>30){clearInterval(iv);m.className='msg warn';m.textContent='Tidak ada respons. Muat ulang halaman.'}},1500)}
$('autoMask').addEventListener('change',function(){var m=0;Array.prototype.forEach.call($('autoMask').querySelectorAll('input'),function(c){if(c.checked)m|=1<<(+c.getAttribute('data-b'))});call('/automask?val='+m,'Halaman otomatis disimpan').then(loadFeat)});
$('prOn').addEventListener('change',function(){call('/prayer?on='+(this.checked?1:0),this.checked?'Pengingat sholat aktif':'Pengingat sholat mati')});
$('prList').addEventListener('change',function(e){if(!e.target.classList.contains('sw'))return;var m=0;Array.prototype.forEach.call($('prList').querySelectorAll('input.sw'),function(c){if(c.checked)m|=1<<(+c.getAttribute('data-k'))});prSig='';call('/prayer?mask='+m,'Pengingat disimpan').then(loadFeat)});
$('prBtn').addEventListener('click',function(){var b=this;b.classList.add('spin');b.disabled=true;get('/prayerrefresh',15000).then(function(r){toast(r.ok?'Jadwal sholat diperbarui':'Gagal mengambil jadwal',r.ok?'':'bad');prSig='';return loadFeat()}).catch(function(){toast('Perangkat tidak merespons','bad')}).then(function(){b.classList.remove('spin');b.disabled=false})});
document.querySelectorAll('[data-g]').forEach(function(b){b.addEventListener('click',function(){var e=$('gAdj');e.value=Math.min(50,Math.max(-50,(parseFloat(e.value)||0)+(+b.getAttribute('data-g')))).toFixed(1).replace(/\.0$/,'');gDirty=true;$('gSave').disabled=false})});
$('gAdj').addEventListener('input',function(){gDirty=true;$('gSave').disabled=false});
$('gSave').addEventListener('click',function(){var v=Math.min(50,Math.max(-50,parseFloat($('gAdj').value)||0));$('gAdj').value=v;call('/gold?adj='+v,'Penyesuaian disimpan').then(function(){gDirty=false;$('gSave').disabled=true;return loadFeat()})});
$('gBtn').addEventListener('click',function(){var b=this;b.classList.add('spin');b.disabled=true;get('/gold?refresh=1',20000).then(function(r){toast(r.ok?'Harga emas diperbarui':'Gagal mengambil harga emas',r.ok?'':'bad');return loadFeat()}).catch(function(){toast('Perangkat tidak merespons','bad')}).then(function(){b.classList.remove('spin');b.disabled=false})});
$('wlList').addEventListener('click',function(e){var b=e.target.closest('button');if(!b)return;
if(b.hasAttribute('data-del')){var i=b.getAttribute('data-del'),nm=(lastW&&lastW.list[i])||'';ask('Hapus WiFi?','"'+nm+'" dihapus dari daftar tersimpan.','Hapus').then(function(ok){if(!ok)return;get('/wifidel?i='+i).then(function(r){if(!r.ok)throw 0;toast('WiFi dihapus');wlSig='';loadWifis()}).catch(function(){toast('Perangkat tidak merespons','bad')})})}
else if(b.hasAttribute('data-use')){var k=b.getAttribute('data-use'),n2=(lastW&&lastW.list[k])||'';ask('Pindah WiFi?','ESP32 mencoba tersambung ke "'+n2+'" (maks. 20 detik). Jika gagal, kembali ke WiFi sebelumnya.','Pindah').then(function(ok){if(!ok)return;var m=$('wifiMsg');m.className='msg warn';m.textContent='Mengecek WiFi "'+n2+'"...';get('/wifiuse?i='+k).then(function(r){return r.text()}).then(function(t){if(t.indexOf('TRYING:')==0)waitWifi(n2);else{m.className='msg bad';m.textContent=t.replace(/^BUSY:/,'')}}).catch(function(){m.className='msg warn';m.textContent='Koneksi terputus karena ESP32 berganti WiFi. Muat ulang dalam 20 detik.';if(!demo)setTimeout(function(){location.reload()},22000)})})}});
$('seg').addEventListener('click',function(e){var b=e.target.closest('button');if(b)call('/mode?val='+b.getAttribute('data-m'))});
$('autoMode').addEventListener('change',function(){call('/auto?val='+(this.checked?1:0))});
$('wxBtn').addEventListener('click',function(){var b=this;b.classList.add('spin');b.disabled=true;get('/weather',12000).then(function(r){if(!r.ok)throw 0;return get('/forecast',15000)}).then(function(r){toast(r.ok?'Cuaca & prakiraan diperbarui':'Prakiraan gagal diperbarui',r.ok?'':'bad');return poll().then(loadFeat)}).catch(function(){toast('Perangkat tidak merespons','bad')}).then(function(){b.classList.remove('spin');b.disabled=false})});
$('autoBright').addEventListener('change',function(){call('/autobright?val='+(this.checked?1:0))});
$('bright').addEventListener('input',pr);$('bright').addEventListener('change',function(){call('/brightness?val='+this.value)});
function sa(){var t=($('alarmTime').value||'07:00').split(':');call('/alarm?hour='+(+t[0])+'&min='+(+t[1])+'&en='+($('alarmEn').checked?1:0),'Alarm disimpan')}
$('alarmEn').addEventListener('change',sa);$('alarmTime').addEventListener('change',sa);
function ss(){call('/sleep?start='+($('sleepStart').value||'22:00')+'&end='+($('sleepEnd').value||'06:00')+'&en='+($('sleepEn').checked?1:0),'Jadwal disimpan')}
['sleepEn','sleepStart','sleepEnd'].forEach(function(i){$(i).addEventListener('change',ss)});
$('beepBtn').addEventListener('click',function(){call('/beep','Buzzer berbunyi')});
function ev(){var a=Math.min(600,Math.max(1,parseInt($('eyeMin').value)||4)),b=Math.min(600,Math.max(1,parseInt($('eyeMax').value)||20));if(b<a)b=a;return[a,b]}
function eh(){var v=ev();$('eyeHint').textContent='Animasi acak tiap '+v[0]+'–'+v[1]+' detik'}
function et(){dirty=true;$('eyeSave').disabled=false;eh()}
document.querySelectorAll('.stepper button').forEach(function(b){b.addEventListener('click',function(){var e=$(b.getAttribute('data-s'));e.value=Math.min(600,Math.max(1,(parseInt(e.value)||1)+(+b.getAttribute('data-d'))));et()})});
$('eyeMin').addEventListener('input',et);$('eyeMax').addEventListener('input',et);
$('eyeSave').addEventListener('click',function(){var v=ev();$('eyeMin').value=v[0];$('eyeMax').value=v[1];call('/eyes?min='+v[0]+'&max='+v[1],'Jeda animasi disimpan').then(function(){dirty=false;$('eyeSave').disabled=true})});
function halt(t,x){halted=true;clearInterval(timer);$('conn').className='pill off';$('connTxt').textContent=t;$('banTitle').textContent=t;$('banText').textContent=x;$('banBtn').hidden=!demo;$('banner').hidden=false;window.scrollTo(0,0)}
$('banBtn').addEventListener('click',function(){halted=false;$('banner').hidden=true;start()});
$('navBtn').addEventListener('click',function(){ask('Mulai MotoNav?','ESP32 pindah ke mode Bluetooth dan dashboard ini terputus. Kembali lewat perintah dari aplikasi MotoNav.','Mulai').then(function(ok){if(!ok)return;get('/switchnav').then(function(){halt('MotoNav aktif','ESP32 dalam mode navigasi Bluetooth. Dashboard terputus sampai kembali ke mode WiFi.')},function(){toast('Perangkat tidak merespons','bad')})})});
$('dsBtn').addEventListener('click',function(){ask('Aktifkan Deep Sleep?','WiFi dan dashboard dimatikan. Keluar dengan klik tombol GPIO 4 tiga kali cepat.','Aktifkan').then(function(ok){if(!ok)return;get('/deepsleep').then(function(r){if(!r.ok){toast('Deep sleep belum bisa diaktifkan sekarang','bad');return}halt('Deep Sleep aktif','WiFi dimatikan. Klik tombol GPIO 4 tiga kali cepat untuk keluar, lalu muat ulang halaman ini.')},function(){toast('Perangkat tidak merespons','bad')})})});
$('pwToggle').addEventListener('click',function(){var p=$('wifiPASS'),h=p.type=='password';p.type=h?'text':'password';this.textContent=h?'Sembunyikan':'Tampilkan'});
$('wifiBtn').addEventListener('click',function(){var s=$('wifiSSID').value.trim(),p=$('wifiPASS').value.trim(),m=$('wifiMsg');
if(!s){m.className='msg bad';m.textContent='Nama WiFi tidak boleh kosong.';return}
m.className='msg';m.textContent='Mengirim...';
get('/setwifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p)).then(function(r){return r.text()}).then(function(t){
if(t.indexOf('TRYING:')==0){m.className='msg warn';m.textContent='Mengecek WiFi "'+s+'" (maks. 20 detik).';$('wifiSSID').value='';$('wifiPASS').value='';waitWifi(s)}
else if(t.indexOf('BUSY:')==0){m.className='msg warn';m.textContent=t.slice(5)}
else{m.className='msg bad';m.textContent='Gagal: '+t}
}).catch(function(){m.className='msg warn';m.textContent='Koneksi terputus karena ESP32 berganti WiFi. Muat ulang dalam 20 detik.';if(!demo)setTimeout(function(){location.reload()},22000)})});
document.addEventListener('pointerdown',function(e){var b=e.target.closest('.btn');if(!b||b.disabled)return;var r=b.getBoundingClientRect(),d=Math.max(r.width,r.height)*2,s=document.createElement('span');s.className='rp';s.style.cssText='width:'+d+'px;height:'+d+'px;left:'+(e.clientX-r.left-d/2)+'px;top:'+(e.clientY-r.top-d/2)+'px';b.appendChild(s);setTimeout(function(){s.remove()},650)});
var ghDirty=false;
function renderTrk(j){['ghPhone','ghOwner','ghRepo','ghBranch','ghDir'].forEach(function(k){if(!ghDirty)sv($(k),j[k.slice(2).toLowerCase()]||'')});
$('ghTokInfo').textContent=j.tokenSet?'Token tersimpan (kosongkan kolom jika tidak ingin menggantinya).':'Token belum diisi. Fine-grained, izin Contents: Read and write, hanya untuk repo ini.';
var t=(j.status||'Belum ada unggahan')+' \u00b7 Menunggu unggah: '+(j.pending|0);if(j.n)t+=' \u00b7 Sedang direkam: '+j.n+' titik, '+nf(j.dist||0)+' m';$('trkInfo').textContent=t;
$('trkFiles').innerHTML=(j.files||[]).map(function(f){return'<div class="row"><div class="t"><small style="font-family:var(--mono)">'+esc(f)+'</small></div><button type="button" class="btn" data-trkdel="'+esc(f)+'">Hapus</button></div>'}).join('')}
function loadTrk(){if(halted)return Promise.resolve();return get('/trk').then(function(r){if(!r.ok)throw 0;return r.json()}).then(renderTrk).catch(function(){})}
['ghPhone','ghOwner','ghRepo','ghBranch','ghDir','ghToken'].forEach(function(k){$(k).addEventListener('input',function(){ghDirty=true})});
$('ghSave').addEventListener('click',function(){var b=new URLSearchParams();['ghPhone','ghOwner','ghRepo','ghBranch','ghDir','ghToken'].forEach(function(k){b.append(k.slice(2).toLowerCase(),$(k).value.trim())});
fetch('/trkcfg',{method:'POST',body:b,cache:'no-store'}).then(function(r){if(!r.ok)throw 0;toast('Pengaturan GitHub disimpan');ghDirty=false;$('ghToken').value='';return loadTrk()}).catch(function(){toast('Perangkat tidak merespons','bad')})});
$('trkUp').addEventListener('click',function(){get('/trkup').then(function(r){if(!r.ok)throw 0;toast('Unggah dijadwalkan');return loadTrk()}).catch(function(){toast('Perangkat tidak merespons','bad')})});
$('trkFiles').addEventListener('click',function(e){var b=e.target.closest('button');if(!b||!b.hasAttribute('data-trkdel'))return;var f=b.getAttribute('data-trkdel');ask('Hapus file jejak?',f+' dihapus dari ESP32 (belum terunggah).','Hapus').then(function(ok){if(!ok)return;get('/trkdel?f='+encodeURIComponent(f)).then(function(r){if(!r.ok)throw 0;toast('File dihapus');loadTrk()}).catch(function(){toast('Perangkat tidak merespons','bad')})})});
function start(){poll();clearInterval(timer);timer=setInterval(function(){if(!halted&&!document.hidden)poll()},2000)}
document.querySelectorAll('.sec').forEach(function(s,i){s.style.setProperty('--n',i)});
fetch('/status',{cache:'no-store'}).then(function(r){if(!r.ok)throw 0;return r.json()}).then(function(){demo=false}).catch(function(){demo=true}).then(function(){
if(demo)$('foot').textContent='Mode pratinjau: data contoh, belum terhubung ke ESP32.';
pr();start();loadFeat();loadWifis();loadTrk();setInterval(function(){if(!document.hidden)loadTrk()},15000);setInterval(function(){if(!document.hidden)loadFeat()},30000);setInterval(function(){if(!document.hidden)loadWifis()},20000);document.body.classList.add('go')});
})();
</script>
</body>
</html>
)UIHTML";
