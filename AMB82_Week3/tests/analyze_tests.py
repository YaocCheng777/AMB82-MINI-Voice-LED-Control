"""Calculate metrics only from filled, actual experimental records; never impute results."""
import csv,collections,pathlib
p=pathlib.Path(__file__).with_name('raw_test_records_40.csv')
rows=list(csv.DictReader(p.open(encoding='utf-8-sig',newline='')))
for phase in ('initial','retest'):
 r=[x for x in rows if x['phase']==phase]
 done=[x for x in r if x['pass_1_fail_0'].strip() in ('0','1')]
 correct=sum(x['pass_1_fail_0'].strip()=='1' for x in done)
 print(f'{phase}: recorded {len(done)}/{len(r)}; correct {correct}; accuracy {correct/len(done)*100:.1f}%' if done else f'{phase}: no measured data')
 if done:
  durations=[]
  for x in done:
   try:durations.append(float(x['response_ms']))
   except (ValueError,TypeError):pass
  if durations:print(f'  mean response {sum(durations)/len(durations):.1f} ms ({len(durations)} measured)')
print('NOTE: blank entries are not counted as successful tests.')
