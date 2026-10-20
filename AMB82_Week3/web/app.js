const questions = [
  {q:'AMB82-MINI 使用哪一種設備擷取手部影像？', options:['麥克風','攝影機','蜂鳴器','溫度感測器'], answer:'B'},
  {q:'本專題伸出 3 根手指代表哪個選項？', options:['A','B','C','D'], answer:'C'},
  {q:'手部關鍵點模型的主要用途為何？', options:['定位手指與手部骨架','連接 Wi-Fi','壓縮聲音','測量電壓'], answer:'A'},
  {q:'目前使用哪個協定在 VLC 觀看即時影像？', options:['FTP','SMTP','DNS','RTSP'], answer:'D'},
  {q:'辨識不穩定時，哪種方式可以降低誤判？', options:['任意結果立即提交','連續多筆相同結果再確認','關閉攝影機','忽略手部位置'], answer:'B'}
];
const $=id=>document.getElementById(id);
const letters='ABCD';
let index=0, score=0, selected=null, confirmed=false, finished=false;
function render(){
  const q=questions[index];
  $('progress').textContent=finished?'已完成':`第 ${index+1} 題 / ${questions.length}`;
  $('score').textContent=`答對 ${score} 題`;
  $('progressbar').style.width=`${(finished?questions.length:index)/questions.length*100}%`;
  $('question').textContent=finished?`測驗完成！共答對 ${score} / ${questions.length} 題`:q.q;
  $('options').innerHTML='';
  if(!finished)q.options.forEach((text,i)=>{
    const el=document.createElement('div');el.className='option';el.dataset.letter=letters[i];
    const mark=document.createElement('strong');mark.textContent=letters[i];
    const label=document.createElement('span');label.textContent=text;
    el.append(mark,label);$('options').appendChild(el);
  });
  $('feedback').textContent=finished?'點選「重新開始」可再次作答。': '請比 1～4 選擇答案，再握拳（0）確認';
  $('feedback').className='feedback';
  $('next').disabled=!confirmed||finished;
  $('next').textContent=index===questions.length-1?'查看結果 →':'下一題 →';
}
function choose(letter){
  if(confirmed||finished||!letters.includes(letter))return;
  selected=letter;
  document.querySelectorAll('.option').forEach(el=>{
    el.classList.toggle('selected',el.dataset.letter===letter);
    el.classList.remove('correct','wrong');
  });
  $('feedback').textContent=`已選擇 ${letter}，請握拳（0）確認作答`;
  $('feedback').className='feedback';
}
function confirmAnswer(){
  if(confirmed||finished||!selected)return;
  confirmed=true;
  const q=questions[index],correct=selected===q.answer;
  if(correct)score++;
  document.querySelectorAll('.option').forEach(el=>{
    if(el.dataset.letter===selected)el.classList.add(correct?'correct':'wrong');
    if(el.dataset.letter===q.answer)el.classList.add('correct');
  });
  $('feedback').textContent=correct?`✓ 回答正確！選擇 ${selected}`:`✕ 回答錯誤：你選 ${selected}，正確答案為 ${q.answer}`;
  $('feedback').className='feedback '+(correct?'good':'bad');
  $('score').textContent=`答對 ${score} 題`;
  $('next').disabled=false;
}
function nextQuestion(){
  if(!confirmed||finished)return;
  if(index===questions.length-1){finished=true;render();return;}
  index++;selected=null;confirmed=false;render();
}
$('next').addEventListener('click',nextQuestion);
$('restart').addEventListener('click',()=>{index=0;score=0;selected=null;confirmed=false;finished=false;render();});
function onEvent(event){
  if(event.type==='connection'){
    const s=event.status;
    $('connection').textContent=({connected:'AMB82 已連接',waiting:'等待連線',disconnected:'COM 埠未連接',pyserial_missing:'未安裝 pyserial',demo:'模擬模式'})[s]||s;
    $('dot').className='dot '+((s==='connected'||s==='demo')?'ok':'bad');
  }else if(event.type==='preview'||event.type==='gesture'){
    const n=event.fingers;
    const name=n===0?'CONFIRM':n===5?'NEXT':letters[n-1];
    $('letter').textContent=name;
    $('gesture').textContent=`目前手勢：${n}（${name}）`;
    $('samples').textContent=event.type==='gesture'?`已辨識：${name}`:`穩定確認 ${event.samples} / ${event.required} 筆`;
    if(event.type==='gesture'){
      if(n>=1&&n<=4)choose(letters[n-1]);
      else if(n===0)confirmAnswer();
      else if(n===5)nextQuestion();
    }
  }else if(event.type==='released'){
    $('gesture').textContent='目前手勢：—';
    $('letter').textContent='—';
    $('samples').textContent='等待下一個手勢';
  }
}
async function init(){
  try{
    const r=await fetch('/api/status');const data=await r.json();
    $('mode').textContent=`模式：${data.demo?'模擬':'USB Serial'}`;
    $('demo-controls').hidden=!data.demo;
  }catch(e){$('connection').textContent='伺服器無回應';}
  const stream=new EventSource('/events');
  stream.onmessage=e=>{try{onEvent(JSON.parse(e.data));}catch(err){console.error(err);}};
  stream.onerror=()=>{$('connection').textContent='正在重新連線網頁伺服器…';$('dot').className='dot bad';};
}
document.querySelectorAll('[data-fingers]').forEach(button=>button.addEventListener('click',async()=>{
  try{await fetch('/api/demo',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({fingers:Number(button.dataset.fingers)})});}
  catch(e){console.error(e);}
}));
render();init();
