from pathlib import Path
from zipfile import ZipFile
from difflib import unified_diff
import sys,json
sys.path.insert(0,r'C:/Users/kicof/AppData/Local/Temp/gridopoly-player-short-20261014/deps')
from gerbonara import GerberFile
p=Path(r'C:/Users/kicof/AppData/Local/Temp/gridopoly-order-independent-20261014')
g=GerberFile.open(p/'order-TopLayer.GTL')
for n in range(1317,1327):print(n,str(g.objects[n]))
old=(p/'order-TopLayer.GTL').read_text().splitlines();new=(p/'rc5-TopLayer.GTL').read_text().splitlines()
d=list(unified_diff(old,new,fromfile='order',tofile='rc5',n=4))
(p/'order-to-rc5-top.diff').write_text('\n'.join(d))
print('diff lines',len(d))
for n,line in enumerate(old):
 if 'X4100068Y-500126' in line or 'X4100068Y-394462' in line:
  print('RAW',n+1, '\n'.join(old[n-3:n+6]))
