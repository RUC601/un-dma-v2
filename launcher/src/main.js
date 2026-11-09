import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
const $=id=>document.getElementById(id);
let language=1,update={phase:'idle'},availableVersion='',launching=false;
function error(e){$('error').textContent=String(e);$('error').hidden=false;}
function labels(){
 const en=language===0;
 document.documentElement.lang=document.querySelector('.unf-window').lang=en?'en':'zh-CN';
 $('unf-authorize').textContent=en?'AUTHORIZATION':'授权';
 $('unf-title').textContent=en?'LOG IN':'登录';$('unf-account-label').textContent=en?'Username':'账号';
 $('unf-password-label').textContent=en?'Password':'密码';$('unf-language-label').textContent=en?'English':'中文';
 $('unf-eye').ariaLabel=$('unf-password').type==='text'?(en?'Hide password':'隐藏密码'):(en?'Show password':'显示密码');
 document.querySelectorAll('[data-language]').forEach(b=>b.setAttribute('aria-pressed',String((b.dataset.language==='en'?0:1)===language)));
 renderUpdate(update);
}
function closeLanguage(focus=false){$('unf-language-popup').hidden=true;$('unf-language-trigger').ariaExpanded='false';if(focus)$('unf-language-trigger').focus();}
$('unf-eye').onclick=()=>{const p=$('unf-password');p.type=p.type==='text'?'password':'text';$('unf-eye').ariaPressed=String(p.type==='password');labels();};
$('unf-language-trigger').onclick=()=>{const p=$('unf-language-popup');p.hidden=!p.hidden;$('unf-language-trigger').ariaExpanded=String(!p.hidden);if(!p.hidden)p.querySelector('[aria-pressed="true"]').focus();};
document.querySelectorAll('[data-language]').forEach(b=>b.onclick=async()=>{language=b.dataset.language==='en'?0:1;labels();closeLanguage(true);try{await invoke('set_language',{language});}catch(e){error(e);}});
function closeUpdate(focus=false){$('update-panel').hidden=true;$('update').ariaExpanded='false';if(focus)$('update').focus();}
document.addEventListener('pointerdown',e=>{if(!e.target.closest('.unf-language'))closeLanguage();if(!e.target.closest('#update-panel')&&!e.target.closest('#update'))closeUpdate();});
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if(!$('update-panel').hidden)closeUpdate(true);else closeLanguage(true);}});
async function authorize(){if(launching)return;launching=true;$('error').hidden=true;$('unf-authorize').disabled=true;try{await invoke('authorize');}catch(e){error(e);}finally{launching=false;$('unf-authorize').disabled=false;}}
$('unf-authorize').onclick=authorize;$('unf-form').onsubmit=e=>{e.preventDefault();authorize();};
$('close').onclick=()=>invoke('close_launcher').catch(error);
$('update').onclick=()=>{const open=$('update-panel').hidden;$('update-panel').hidden=!open;$('update').ariaExpanded=String(open);};
function renderUpdate(s){
 if(s.phase==='current')availableVersion='';else if(s.version)availableVersion=s.version;
 update=s;const en=language===0;$('update').hidden=!availableVersion||['idle','current','checking'].includes(s.phase);
 if($('update').hidden)closeUpdate();
 const tips=en?{available:'New version available',downloading:'Downloading update',ready:'Update ready',error:'Update failed · Click to retry',stopping:'Saving and closing'}:{available:'发现新版本',downloading:'正在下载更新',ready:'更新已就绪',error:'更新失败 · 点击重试',stopping:'正在保存并关闭'};
 $('update').title=$('update').ariaLabel=tips[s.phase]||(en?'New version available':'发现新版本');
 $('update-title').textContent=(en?'Update ':'更新 ')+(s.version||availableVersion);
 const names=en?{available:'Ready to download',downloading:'Downloading',ready:'Ready to restart',error:'Update failed',stopping:'Saving and closing'}:{available:'可下载更新',downloading:'正在下载',ready:'可重启更新',error:'更新失败',stopping:'正在保存并关闭'};
 $('update-status').textContent=s.error||names[s.phase]||'';
 $('download').hidden=s.phase!=='downloading';if(s.total)$('download').value=s.received/s.total*100;else $('download').removeAttribute('value');
 const b=$('update-action');b.disabled=['downloading','stopping'].includes(s.phase);b.textContent=s.phase==='ready'?(en?'Restart and update':'重启并更新'):s.phase==='error'?(en?'Retry':'重试'):(en?'Download':'下载');
}
$('update-action').onclick=()=>invoke(update.phase==='ready'?'install_update':update.phase==='error'?'check_update':'download_update').catch(e=>renderUpdate({...update,phase:'error',error:String(e)}));
await listen('runtime-error',e=>error(e.payload));
await listen('update-state',e=>renderUpdate(e.payload));
await listen('preferences',e=>{language=e.payload.language??language;document.body.classList.toggle('glass',e.payload.loginGlass!==false);labels();});
try{const p=await invoke('preferences');language=p.language??1;document.body.classList.toggle('glass',p.loginGlass!==false);labels();await invoke('check_update');}catch(e){renderUpdate({phase:'error',error:String(e)});}
