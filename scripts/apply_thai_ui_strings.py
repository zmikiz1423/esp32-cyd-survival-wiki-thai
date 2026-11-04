"""Translate exact C++ string literals without touching identifiers/paths."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
p=root/'firmware/src/main.cpp'
mapping=json.loads((root/'scripts/thai_ui_strings.json').read_text('utf-8'))
s=p.read_text('utf-8');counts={}
def replace(m):
    token=m[0]
    try: value=json.loads(token)
    except ValueError:return token
    if value not in mapping:return token
    counts[value]=counts.get(value,0)+1
    return json.dumps(mapping[value],ensure_ascii=False)
s=re.sub(r'"(?:[^"\\\n]|\\.)*"',replace,s)
# UTF-8 Thai labels need room for their byte representation.
s=s.replace('static char meshLabel[12]','static char meshLabel[32]')
p.write_text(s,'utf-8',newline='\n')
print('Translated',sum(counts.values()),'UI literal occurrences; unused keys',sorted(set(mapping)-set(counts)))
