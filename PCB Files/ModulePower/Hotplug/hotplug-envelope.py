"""Analytical checks only; not a SPICE simulation or measured qualification."""
import json,pathlib,itertools
r1,r2,r3=220e3,4.7e3,10e3
uv=[];ov=[];uvfall=[]
for a,b,c in itertools.product([.99,1.01],repeat=3):
 R1,R2,R3=r1*a,r2*b,r3*c
 leak_uv=100e-9*(R1+R1*R3/(R2+R3));leak_ov=100e-9*(2*R1+R2)
 uv +=[1.18*(1+R1/(R2+R3))-leak_uv,1.23*(1+R1/(R2+R3))+leak_uv]
 uvfall +=[1.09*(1+R1/(R2+R3))-leak_uv,1.135*(1+R1/(R2+R3))+leak_uv]
 ov +=[1.18*(R1+R2+R3)/R3-leak_ov,1.23*(R1+R2+R3)/R3+leak_ov]
cap_nom=(47+2.2*2)*1e-6
cap_max=(47*1.2+2*2.2*1.1*1.15)*1e-6
slope_nom=1.98e-6*24.6/1e-6;slope_max=2.33e-6*25.2/(1e-6*.9*.85)
branch_charge_max=cap_max*slope_max
aux_max=482e-6+24/(r1*.99+r2*.99+r3*.99)+24/(47e3*.99)
result=dict(
 assumptions=['24 V bus; 1% resistors; capacitor initial tolerance 10% and X7R 15% are included.', 'Resistor temperature coefficient, capacitor aging, parasitics, converter load interaction and wiring dynamics are not bounded here.', 'LMR16030 SS current has a typical value only; 0.55 s is NOT a guaranteed minimum.', '60 W is total power drawn at the 24 V bus including local conversion losses and protection overhead.'],
 uvlo_nominal_V=1.2*(r1+r2+r3)/(r2+r3),uvlo_initial_bounds_V=[min(uv),max(uv)],uvlo_falling_bounds_V=[min(uvfall),max(uvfall)],
 ovp_nominal_V=1.2*(r1+r2+r3)/r3,ovp_initial_bounds_V=[min(ov),max(ov)],
 branch_current_limit_A={'nominal_table':.152,'minimum_with_R1pct':.145/1.01,'maximum_with_R1pct':.159/.99},
 efuse_ramp_s_nominal=24/slope_nom,local_cap_uF=cap_nom*1e6,local_cap_upper_uF=cap_max*1e6,
 local_cap_charge_A_nominal=cap_nom*slope_nom,local_cap_charge_A_bound=branch_charge_max,
 simultaneous_40_cap_charge_A=40*branch_charge_max,simultaneous_40_precharge_and_aux_A=40*(branch_charge_max+aux_max),
 efuse_flt_deglitch_s_nominal=(750+573*1000)*1e-6,
 buck_soft_start_s_nominal=2.2e-6*.750/3e-6,
 en_fault_low_V_estimate=(28.2/(47e3*.99)+4.6e-6)*145,
 efuse_precharge_loss_J_upper=.5*cap_max*24**2,
 fixed_bus24V_single_module_1W_A=1/24,
 full_system_60W_bus_A=60/24,
 note='FLT delays the buck until local capacitors finish charging. Delay terms must not simply be treated as independent guaranteed limits. Hotplug and full-load stability still require PCB + bench testing.')
assert max(uv)<22.8 and min(ov)>25.2
assert branch_charge_max<.01
assert result['en_fault_low_V_estimate']<1.05
assert 1.5/22.8<result['branch_current_limit_A']['minimum_with_R1pct']
if __name__=='__main__':
 dest=pathlib.Path(__file__).with_suffix('.json');dest.write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps(result,indent=2))
