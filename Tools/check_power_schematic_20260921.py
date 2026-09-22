"""Read-only checks of captured EasyEDA schematic netlists, not a SPICE simulation.

Run from the repository root. Snapshot acquisition and the component definitions
must be independently reviewed; these checks do not certify hardware operation.
"""
import hashlib
import itertools
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
P = ROOT / 'PCB Files' / 'DesignAudit-2026-09-21'
COMMON = {
 'U201': ['HP_RAW','HP_UV','HP_OV','','HP_RTN','GND','HP_ILIM','HP_DT','HP_RUN','24V_BUCK_IN','HP_RTN'],
 'U23': ['BUCK24_BOOT','24V_BUCK_IN','HP_RUN','BUCK24_RT','BUCK24_FB','HP_SS','GND','BUCK24_SW','GND'],
 'U24': ['24V_BUS','HP_RAW'], 'D6':['HP_RAW','GND'],
 'R201':['HP_RAW','HP_UV'], 'R202':['HP_UV','HP_OV'],
 'R204':['HP_OV','HP_RTN'], 'R205':['HP_ILIM','HP_RTN'],
 'R206':['24V_BUCK_IN','HP_RUN'], 'C202':['HP_DT','HP_RTN'],
 'C203':['HP_SS','GND'], 'C53':['GND','HP_RAW'],
 'C52':['24V_BUCK_IN','GND'], 'C54':['24V_BUCK_IN','GND'],
 'C55':['24V_BUCK_IN','GND'], 'C56':['BUCK24_BOOT','BUCK24_SW'],
 'R34':['BUCK24_FB','5V_FROM_24'], 'R35':['BUCK24_FB','GND'],
 'R39':['GND','BUCK24_RT'], 'D3':['BUCK24_SW','GND'],
 'L2':['BUCK24_SW','5V_FROM_24'],
 **{f'C{i}':['5V_FROM_24','GND'] for i in range(35,39)},
 'JIN1':['24V_BUS','24V_BUS','24V_BUS','ORDER_IN','BUS_A','BUS_B'],
 'JOUT1':['24V_BUS','24V_BUS','24V_BUS','ORDER_OUT','BUS_A','BUS_B'],
 'JIN3':['GND']*3, 'JOUT3':['GND']*3,
}
ENTRY = {'J301':['DC24_IN','GND',''], 'F301':['DC24_IN','DC24_FUSED'],
 'D301':['24V_BUS','DC24_FUSED'], 'D302':['24V_BUS','GND'],
 'C301':['GND','24V_BUS'], 'C302':['GND','24V_BUS']}
PARTS = {'U201':'TPS26621DRCR','U23':'LMR16030SDDAR',
 'J301':'DC-005-5A-2.0','F301':'S1206-F-4.0A','D301':'SS56B','D302':'SMBJ26A'}
VALUES = {'R201':'220kΩ','R202':'4.7kΩ','R204':'10kΩ','R205':'44.2kΩ',
 'R206':'47kΩ','C202':'1uF','C203':'2.2uF','R34':'100kΩ','R35':'17.8kΩ',
 'R39':'49.9kΩ','L2':'8.2uH'}
result = {'scope':'DC24 entry and common branch power schematic only; no PCB/coil/display certification',
          'boards':{}, 'failures':[]}
boards={}
for board in ['corner','long']:
 raw=(P/(board+'-sch.net')).read_bytes()
 ns=json.loads(raw)['components']; cm={c['props']['Designator']:c for c in ns.values()}
 boards[board]=cm
 checks=dict(COMMON)
 if board=='corner':checks.update(ENTRY)
 count=0
 for ref,nets in checks.items():
  for pin,expected in enumerate(nets,1):
   actual=cm[ref]['pinInfoMap'].get(str(pin),{}).get('net',None);count+=1
   if actual!=expected:result['failures'].append([board,ref,str(pin),expected,actual])
 for ref,part in PARTS.items():
  if ref in cm and cm[ref]['props'].get('Manufacturer Part')!=part:
   result['failures'].append([board,ref,'part mismatch'])
 for ref,val in VALUES.items():
  if cm[ref]['props'].get('Value')!=val:result['failures'].append([board,ref,'value mismatch'])
 for ref in COMMON:
  if cm[ref]['props'].get('Add into BOM')=='no':result['failures'].append([board,ref,'unexpected DNP'])
 result['boards'][board]={'sha256':hashlib.sha256(raw).hexdigest(),
  'total_components':len(cm),'power_components_checked':len(checks),'power_pins_checked':count}
for ref in COMMON:
 a,b=boards['corner'][ref],boards['long'][ref]
 for k in ['Manufacturer Part','Supplier Part','Value']:
  if a['props'].get(k)!=b['props'].get(k):result['failures'].append(['board mismatch',ref,k])

r1,r2,r3=220e3,4.7e3,10e3
uv=[];ov=[];uvfall=[]
for a,b,c in itertools.product([.99,1.01],repeat=3):
 R1,R2,R3=r1*a,r2*b,r3*c
 eu=100e-9*(R1+R1*R3/(R2+R3));eo=100e-9*(2*R1+R2)
 uv.extend([1.18*(1+R1/(R2+R3))-eu,1.23*(1+R1/(R2+R3))+eu])
 uvfall.extend([1.09*(1+R1/(R2+R3))-eu,1.135*(1+R1/(R2+R3))+eu])
 ov.extend([1.18*(R1+R2+R3)/R3-eo,1.23*(R1+R2+R3)/R3+eo])
ilim_min=.145/1.01;ilim_max=.159/.99
slope=1.98e-6*24.6/1e-6
cmx=(47*1.2+4.4*1.1*1.15)*1e-6
smax=2.33e-6*25.2/(1e-6*.9*.85)
aux=482e-6+25.2/(234.7e3*.99)+25.2/(47e3*.99)
result['calculation']={
 'uvlo_nominal_V':1.2*(r1+r2+r3)/(r2+r3),'uvlo_initial_bounds_V':[min(uv),max(uv)],
 'uvlo_fall_initial_bounds_V':[min(uvfall),max(uvfall)],
 'ovp_nominal_V':1.2*(r1+r2+r3)/r3,'ovp_initial_bounds_V':[min(ov),max(ov)],
 'ilim_nominal_A':.152,'ilim_initial_bounds_A':[ilim_min,ilim_max],
 'buck_nominal_V':.750*(1+100/17.8),
 'buck_full_temp_reference_and_initial_R_bounds_V':[.735*(1+100*.99/(17.8*1.01)),.765*(1+100*1.01/(17.8*.99))],
 'buck_nominal_frequency_kHz':500,'buck_softstart_typical_s':2.2e-6*.75/3e-6,
 'efuse_ramp_typical_s_at_24V':24/slope,
 'efuse_flt_deglitch_typical_s':(750+573*1000)*1e-6,
 'precharge_40_nominal_A':40*51.4e-6*slope,
 'precharge_40_cap_tolerance_plus_aux_bound_A':40*(cmx*smax+aux),
 'en_fault_low_V_estimate':(29.5/(47e3*.99)+4.6e-6)*145,
 'module_input_power_at_20V_lower_ilim_W':20*ilim_min,
 'module_5V_power_at_20V_lower_ilim_eta85_example_W':20*ilim_min*.85,
 'forty_simultaneous_at_branch_limit_A':[40*ilim_min,40*ilim_max],
 'entry_60W_bus_at_23_3V_A':60/23.3,
 'entry_diode_heat_at_Vf_0_7_example_W':60/23.3*.7,
 'examples_bus_power_W_per_board':{str(n):60/n for n in [16,24,32,40]},
 'assumptions':['60W denotes total 24V bus input power, including module conversion losses.',
 'UVLO/OVP use datasheet threshold/leakage and initial resistor tolerance; resistor drift is not bounded.',
 'Precharge excludes DC connector transients, 5V load startup, wire impedance, and converter-loop dynamics.',
 'SS charge current is typical only. This is not transient simulation or a guaranteed delay.',
 '85% efficiency and 0.7V diode drop are calculation assumptions, not measured operating points.',
 'Current-limit figures are protection thresholds, not recommended continuous operating currents.']}
(P/'power-schematic-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False,indent=2))
if result['failures']:raise SystemExit(1)
