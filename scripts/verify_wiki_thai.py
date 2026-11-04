"""Audit the Thai wiki against preserved source IDs, values and link targets.

Run after editorial changes, before rebuilding search/PDF/SD packages.
"""
from collections import Counter
from pathlib import Path
import json
import re

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'.thai-work/original/sdcard/wiki'
TARGET=ROOT/'sdcard/wiki'

def fields(text):
    _,front,body=text.split('---',2)
    return dict(line.split(':',1) for line in front.strip().splitlines()),body

def main():
    errors=[]
    paths=sorted((TARGET/'articles').glob('*.md'))
    numeric_values=0
    link_count=0
    titles={}
    for path in paths:
        ident=path.stem
        source_path=SOURCE/'articles'/path.name
        if not source_path.exists():
            errors.append(ident+': original source missing')
            continue
        source,source_body=fields(source_path.read_text(encoding='utf-8-sig'))
        target,target_body=fields(path.read_text(encoding='utf-8-sig'))
        titles[ident]=target['title'].strip()
        for field in ('category','order'):
            if source[field]!=target[field]:errors.append(ident+': changed '+field)
        for field in ('title','summary'):
            if not re.search('[ก-๙]',target[field]):errors.append(ident+': non-Thai '+field)
        if not re.search('[ก-๙]',target_body):errors.append(ident+': body not Thai')
        if '\ufffd' in target_body or 'QQQURL' in target_body:errors.append(ident+': damaged text')
        numbers=lambda s:Counter(re.findall(r'\b\d+(?:\.\d+)?\b',s))
        missing=numbers(source_body)-numbers(target_body)
        if missing:errors.append(ident+': missing numeric values '+str(missing))
        numeric_values+=sum(numbers(source_body).values())
        links=lambda s:re.findall(r'\]\(([^)]+)\)',s)
        if links(source_body)!=links(target_body):errors.append(ident+': link/image destination changed')
        link_count+=len(links(source_body))
        for destination in links(target_body):
            local=destination.split('#',1)[0]
            if not local or '://' in local:continue
            expected=TARGET/local if local.startswith('img/') else path.parent/local
            if not expected.exists():errors.append(ident+': destination missing '+destination)
    catalog=[]
    for line in (TARGET/'catalog.tsv').read_text(encoding='utf-8-sig').splitlines():
        if not line.strip() or line.startswith('#'):continue
        ident,category,title=line.split('\t')
        catalog.append(ident)
        if titles.get(ident)!=title:errors.append(ident+': catalog/title mismatch')
    if len(paths)!=118 or len(catalog)!=118 or len(set(catalog))!=118:
        errors.append('Expected 118 unique articles/catalog rows')
    if set(catalog)!=set(titles):errors.append('Article/catalog ID sets differ')
    original_ids=[line.split('\t')[:2] for line in (SOURCE/'catalog.tsv').read_text(encoding='utf-8-sig').splitlines() if line.strip() and not line.startswith('#')]
    target_ids=[line.split('\t')[:2] for line in (TARGET/'catalog.tsv').read_text(encoding='utf-8-sig').splitlines() if line.strip() and not line.startswith('#')]
    if original_ids!=target_ids:errors.append('Catalog ID/category/order changed')
    report={'articles':len(paths),'catalog_rows':len(catalog),'original_numeric_values_preserved':numeric_values,
            'original_link_and_image_destinations_preserved':link_count,'max_article_bytes':max(p.stat().st_size for p in paths),
            'errors':errors,'passed':not errors}
    output=ROOT/'.thai-work/wiki-verification.json'
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    raise SystemExit(bool(errors))

if __name__=='__main__':main()
