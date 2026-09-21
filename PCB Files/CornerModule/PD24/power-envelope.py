"""Analytical design envelope only; not a SPICE, loop-stability or hardware sign-off.
Run with Python 3 + numpy. All voltages refer to nominal 24 V bus unless stated.
"""
import math,json,pathlib
import numpy as np

def ring(n,total_w,segment_loop_r,source_v):
    # One source at node 0. Each of the other n-1 nodes takes equal constant power.
    # Each edge R is positive-wire + return-wire resistance. Uniform physical ring.
    # Putting all power away from source is conservative vs powered corner's own load.
    if n<=1:return dict(nodes=n,total_w=total_w,min_v=source_v,wire_loss_w=0,source_a=total_w/source_v)
    v=np.full(n-1,source_v);power=total_w/(n-1)
    for _ in range(100):
        a=np.diag(np.full(n-1,2/segment_loop_r))
        for i in range(n-2):a[i,i+1]=a[i+1,i]=-1/segment_loop_r
        rhs=-power/v;rhs[0]+=source_v/segment_loop_r;rhs[-1]+=source_v/segment_loop_r
        nv=np.linalg.solve(a,rhs)
        if np.max(abs(nv-v))<1e-9:break
        if min(nv)<=0:raise ValueError('Voltage-collapse candidate')
        v=(v+nv)/2
    vv=np.concatenate(([source_v],nv));currents=(vv-np.roll(vv,-1))/segment_loop_r
    return dict(nodes=n,total_w=total_w,segment_loop_r_ohm=segment_loop_r,source_v=source_v,min_v=float(min(vv)),wire_loss_w=float(sum(currents**2)*segment_loop_r),largest_segment_a=float(max(abs(currents))),source_a=float((2*source_v-nv[0]-nv[-1])/segment_loop_r))

def calculate():
    vnom=.8*(1+294000/10000)
    vlo=.788*(1+294000*.999/(10000*1.001))
    vhi=.812*(1+294000*1.001/(10000*.999))+.000000025*294000*1.001
    ilow=4.185/1.01;ihigh=4.815/.99
    ssnom=2.2e-6*.8/5e-6
    ssmin=2.2e-6*.9*.85*.788/6.35e-6 # explicit -15% X7R temp and -10% C
    capmax=(40*(47*1.2+4.4*1.1+.1*1.1)+200*1.2+20*1.1+.2)*1e-6
    charge=capmax*vhi/ssmin
    loop=[]
    for vin in (18,20,21):
      for c in (160e-6,220e-6,2.68e-3):
        # TI Eq.44 inversion: high-frequency asymptote, gm and gain typical.
        fc=6800*.00131*(.8/vnom)*(vin/vnom)/(2*math.pi*5*.004*c)
        loop.append(dict(vin=vin,cap_f=c,estimated_boost_crossover_hz=fc))
    return dict(status='ENGINEERING PROTOTYPE — hardware validation outstanding',
      assumptions={'efficiency':.90,'pd_input_min_v':18,'pd_input_nominal_v':20,'load_w':60,'power_stage_target_w':72,'ring_is_electrically_parallel':True,'switch_hz_approx':300000,'switch_frequency_tolerance_not_in_loop_estimate':True},
      output={'nominal_before_ideal_diode_v':vnom,'initial_fb_tolerance_min_v':vlo,'initial_fb_and_bias_tolerance_max_v':vhi,'ideal_diode_drop_typical_v':.020,'resistor_temperature_drift_not_in_voltage_bound':True,'bleeder_no_load_w':vnom*vnom/47000,'nominal_60w_output_a':60/24,'nominal_72w_output_a':72/24},
      input={'nominal_60w_at_20v_a':60/.9/20,'min_efuse_current_a':ilow,'max_efuse_current_a':ihigh,'nominal_limit_a':4.5,'available_output_w_18v_90pct_min_limit':18*.9*ilow,'power_stage_target_not_full_corner_guarantee':True},
      soft_start={'nominal_s':ssnom,'estimated_min_s_with_cap_temp':ssmin,'max_40node_cap_f':capmax,'capacitor_charge_current_a':charge,'hypothetical_cpl60w_current_at_3p8v_plus_charge_a':60/3.8+charge,'minimum_buck_valley_limit_25c_resistor_a':.066/(.004*1.01),'startup_not_guaranteed':True},
      compensation={'zero_hz':1/(2*math.pi*6800*2.2e-6),'hf_pole_hz_approx':1/(2*math.pi*6800*1e-9),'deadbeat_cslope_pf':2e-6*4.7e-6/(.004*5)*1e12,'rhp_zero_18v_60w_hz':18**2/(2*math.pi*4.7e-6*60),'estimates':loop,'phase_margin_not_verified':True},
      ring_cases=[ring(n,w,r,vlo-.02)for n,w in [(16,16),(32,32),(40,40),(40,60)]for r in (.05,.075,.1)],
      notes=['0 W means no external load; powered corner and input circuit still consume power.','Minimum-load-free topology is not proof of measured zero-load regulation.','No guarantee for an arbitrary 60 W constant-power load starting at 3.8 V.','Steady cable/connector resistance, transient current and thermal limits require measurement.','Only one externally powered corner; no current-sharing or summed supplies guarantee.'])

if __name__=='__main__':
    result=calculate();destination=pathlib.Path(__file__).with_name('power-envelope.json')
    destination.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    print(json.dumps(result,indent=2,ensure_ascii=False))
