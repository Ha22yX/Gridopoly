"""Gridopoly power review: TI-based small-signal model and explicit engineering envelopes.
Not a switching SPICE model. No hardware measurements are implied.
Python 3, numpy, scipy, matplotlib. Run in this directory to regenerate outputs.
"""
import json, math, pathlib, itertools, sys
import numpy as np
from scipy import linalg
from scipy.integrate import solve_ivp
P=pathlib.Path(__file__).parent
PI=np.pi

def parameters(vin=18.,vout=24.32,power=60.,cap=220e-6,esr=.0175,ind=4.7e-6,fs=300e3,rc=5600.,cc=2.2e-6,ch=1e-9,cs=470e-12,gm=.00127,ri=.020):
 R=vout*vout/max(power,.001);D=1-vin/vout
 se=(4e-6+abs(vin-vout)*2e-6)/cs/ri
 k=(1+se/(vin/ind))*(1-D);q=1/PI/(k-.5)
 km=1/((.5-D)*ri/(fs*ind)+4e-6/cs/fs/vout+2e-6/cs/fs*D)
 k1=.5*ri/(ind*fs)*D*(1-D)+2e-6/cs/fs*D*D
 kd=2+R*(1-D)**2/ri*(1/km+k1/(1-D))
 return dict(vin=vin,vout=vout,power=power,cap=cap,esr=esr,ind=ind,fs=fs,rc=rc,cc=cc,ch=ch,cs=cs,gm=gm,ri=ri,R=R,D=D,q=q,kd=kd,K=(1-D)/ri,Ga=(kd-1)/R,wrhp=(1-D)**2*R/ind)

def bode(a,cpl=False):
 f=np.geomspace(.01,2*a['fs'],5000);s=2j*PI*f
 kd=a['kd']-(2 if cpl else 0);fp=kd/(2*PI*a['R']*a['cap']);am=a['R']*(1-a['D'])/(a['ri']*kd)
 cc,rc,ch,gm=a['cc'],a['rc'],a['ch'],a['gm'];bwcap=gm/(2*PI*8e6)
 fpl=1/(2*PI*(20e6+rc)*cc);fz=1/(2*PI*rc*cc);fph=1/(2*PI*rc*(ch+bwcap))
 H=am*gm*20e6*.8/a['vout']*(1+s/(2*PI*fz))*(1+s*a['cap']*a['esr'])*(1-s/a['wrhp'])/((1+s/(2*PI*fp))*(1+s/(2*PI*fpl))*(1+s/(2*PI*fph))*(1+s/(a['q']*PI*a['fs'])+(s/(PI*a['fs']))**2))
 db=20*np.log10(abs(H));ph=np.unwrap(np.angle(H))*180/PI
 crossings=[]
 for i in np.where(np.diff(np.sign(db))!=0)[0]:
  z=-db[i]/(db[i+1]-db[i]);crossings.append([float(np.exp(np.log(f[i])+z*np.log(f[i+1]/f[i]))),float(180+ph[i]+z*(ph[i+1]-ph[i]))])
 margins=[]
 for i in np.where(np.diff(np.sign(ph+180))!=0)[0]:
  z=(-180-ph[i])/(ph[i+1]-ph[i]);margins.append(float(-db[i]-z*(db[i+1]-db[i])))
 return dict(crossings_hz_deg=crossings,gain_margins_db=margins),(f,db,ph)

def dc(n,power,r,vs=23.89,closed=True):
 if n==1:return dict(min_v=vs,source_a=power/vs,wire_loss_w=0.,max_link_a=0.,voltages=[vs])
 edges=[(i,i+1)for i in range(n-1)]+([(n-1,0)]if closed else [])
 B=np.zeros((n,len(edges)))
 for k,(i,j)in enumerate(edges):B[i,k]=-1;B[j,k]=1
 G=B@B.T/r;v=np.full(n,vs);pn=power/n
 for _ in range(200):
  nv=np.linalg.solve(G[1:,1:],-pn/v[1:]-G[1:,0]*vs)
  if min(nv)<1:raise ValueError('DC voltage collapse')
  if max(abs(nv-v[1:]))<1e-10:break
  v[1:]=.5*(v[1:]+nv)
 v[1:]=nv;i=-B.T@v/r
 return dict(min_v=float(min(v)),source_a=float(pn/v[0]-(B@i)[0]),wire_loss_w=float(r*np.dot(i,i)),max_link_a=float(max(abs(i))),voltages=v.tolist())

def network(a,n=40,r=.04,ell=1e-6,rf=.8,cl=51.4e-6,raw=.1e-6,closed=True):
 """Linearization about each module's constant-power DC operating point.
 eFuse in its ON state; negative incremental load admittance is retained.
 ESR omitted for conservative damping test; very fast contact arc is not modeled.
 Four controller states: two compensation-cap voltages, sample filter x and dx/dt.
 """
 op=dc(n,a['power'],r,a['vout'],closed);v=np.array(op['voltages']);pn=a['power']/n
 edges=[(i,i+1)for i in range(n-1)]+([(n-1,0)]if closed and n>2 else [])
 if n==2 and closed:edges.append((1,0))
 m=len(edges);B=np.zeros((n,m))
 for k,(i,j)in enumerate(edges):B[i,k]=-1;B[j,k]=1
 N=2*n+m+4;A=np.zeros((N,N));cidx=2*n+m
 caps=np.full(n,raw);caps[0]+=a['cap'];vlocal=v-pn/v*rf
 for k in range(n):
  A[k,k]-=1/rf/caps[k];A[k,n+k]+=1/rf/caps[k]
  A[n+k,k]+=1/rf/cl;A[n+k,n+k]+=(-1/rf+pn/vlocal[k]**2)/cl
 A[:n,2*n:2*n+m]=B/caps[:,None]
 A[2*n:2*n+m,:n]=-B.T/ell;A[2*n:2*n+m,2*n:2*n+m]=-r/ell*np.eye(m)
 A[0,0]-=a['Ga']/caps[0]
 K=a['K'];wn=PI*a['fs'];ch=a['ch']+a['gm']/(2*PI*8e6)
 A[0,cidx+2]+=K/caps[0];A[0,cidx+3]-=K/a['wrhp']/caps[0]
 A[cidx,0]=-a['gm']*.8/a['vout']/ch
 A[cidx,cidx]=-(1/20e6+1/a['rc'])/ch;A[cidx,cidx+1]=1/a['rc']/ch
 A[cidx+1,cidx]=1/a['rc']/a['cc'];A[cidx+1,cidx+1]=-1/a['rc']/a['cc']
 A[cidx+2,cidx+3]=1
 A[cidx+3,cidx]=wn*wn;A[cidx+3,cidx+2]=-wn*wn;A[cidx+3,cidx+3]=-wn/a['q']
 # Time in milliseconds reduces numerical scaling for eigenvalue calculations.
 A*=.001
 bal,T=linalg.matrix_balance(A);eig=linalg.eigvals(bal)
 return A,dict(max_real_pole_per_s=float(max(eig.real)*1000),min_local_v=float(min(vlocal)),dc=op),vlocal,caps

def step(a,n,r,pstep,duration=.4,closed=True):
 A,meta,vl,caps=network(a,n=n,r=r,closed=closed)
 b=np.zeros(len(A));b[n:2*n]=-pstep/n/vl/51.4e-6*.001
 bal,T=linalg.matrix_balance(A);invT=np.diag(1/np.diag(T));bb=invT@b
 steady=-linalg.solve(bal,bb)
 eig,U=linalg.eig(bal);coeff=linalg.solve(U,steady.astype(complex));times=np.unique(np.r_[0,np.geomspace(.001,duration*1000,1100)])
 y=(T@(U@(coeff[:,None]*(1-np.exp(eig[:,None]*times[None,:]))))).real
 # Independent ODE check at sparse times; a scaled linear ODE, not switching SPICE.
 check=solve_ivp(lambda t,x:bal@x+bb,(0,duration*1000),np.zeros(len(A)),method='Radau',jac=bal,t_eval=times[::40],rtol=2e-7,atol=1e-8)
 interp=np.column_stack([np.interp(check.t,times,y[k])for k in range(len(A))]).T
 error=float(np.max(abs((T@check.y)[:n]-interp[:n])))
 out=dict(meta,step_w=pstep,source_min_v=float(a['vout']+min(y[0])),source_max_v=float(a['vout']+max(y[0])),worst_incremental_bus_drop_v=float(-min(y[:n].ravel())),worst_incremental_local_drop_v=float(-min(y[n:2*n].ravel())),ode_crosscheck_error_v=error)
 return out,(times*.001,y[:n])

def run():
 results={'model':'TI SNVC208 1.0.2 formulas reimplemented; not official switching SPICE','hardware_measured':False,'assumptions':{'bus_load_limit_w':60,'single_module_design_load_w':1,'loop_r_per_link_max_ohm_at_temperature':.04,'loop_l_per_link_max_h':1e-6,'pd_source': 'single 20V/5A contract; 5A e-marked cable; input >=18V','efficiency_floor_assumed':.90,'controller_gm_sweep_is_engineering_assumption_not_guaranteed_spec':True,'linear_eFuse_reverse_current_blocking_not_modeled':True,'fast_bus_parasitics_and_PTC_position_simplified':True,'large_steps_are_linear_extrapolations_not_startup_or_hotplug_simulations':True,'main_compensation_final':[5600,2.2e-6,1e-9]}}
 rng=np.random.default_rng(20260921);cases=[]
 for _ in range(1200):
  a=parameters(vin=rng.uniform(18,21),power=rng.choice([.05,1,16,32,40,60,72]),cap=rng.uniform(160e-6,270e-6),esr=rng.uniform(.005,.04),ind=4.7e-6*rng.uniform(.8,1.2),fs=rng.uniform(270e3,330e3),rc=5600*rng.uniform(.99,1.01),cc=2.2e-6*rng.uniform(.765,1.265),ch=1e-9*rng.uniform(.765,1.265),cs=470e-12*rng.uniform(.95,1.05),gm=.00127*rng.uniform(.75,1.25),ri=.020*rng.uniform(.94,1.06))
  for cp in [False,True]:
   b,_=bode(a,cp);cases.append(dict(params=a,cpl=cp,**b))
 for vals in itertools.product([18,21],[160e-6,270e-6],[.005,.04],[.8,1.2],[270e3,330e3],[.75,1.25],[.94,1.06],[.765,1.265],[.765,1.265],[.95,1.05]):
  vin,cap,esr,il,fs,g,rs,ccf,chf,csf=vals
  a=parameters(vin=vin,power=72,cap=cap,esr=esr,ind=4.7e-6*il,fs=fs,gm=.00127*g,ri=.02*rs,cc=2.2e-6*ccf,ch=1e-9*chf,cs=470e-12*csf)
  for cp in [False,True]:
   b,_=bode(a,cp);cases.append(dict(params=a,cpl=cp,**b))
 results['loop_scan']={'cases':len(cases),'worst_phase':min(cases,key=lambda x:min(z[1]for z in x['crossings_hz_deg'])),'worst_gain_margin':min(cases,key=lambda x:min(x['gain_margins_db'])),'min_crossover_hz':min(z[0]for x in cases for z in x['crossings_hz_deg']),'max_crossover_hz':max(z[0]for x in cases for z in x['crossings_hz_deg'])}
 results['dc_cases']=[dict(nodes=n,power_w=w,r_ohm=r,closed=closed,**dc(n,w,r,closed=closed))for n,w in [(1,1),(16,16),(24,24),(32,32),(40,40),(40,60)]for r in [.02,.04,.075]for closed in [True,False]]
 net=[]
 for n,r,ell,rf,cl in itertools.product([1,16,40],[.02,.04],[.25e-6,1e-6],[.5,1.5],[37.6e-6,62e-6]):
  a=parameters(power=min(n*1.5,60),cap=160e-6)
  _,m,_,_=network(a,n,r,ell,rf,cl)
  net.append(dict(nodes=n,r=r,ell=ell,rf=rf,cl=cl,**m))
 results['network_poles']={'cases':len(net),'worst':max(net,key=lambda x:x['max_real_pole_per_s']),'unstable_count':sum(x['max_real_pole_per_s']>=0 for x in net)}
 traces={};steps=[]
 for label,n,r,power,delta,closed in [('distributed_1w_step',40,.04,40,1,True),('linear_plus60w_extrapolation',40,.04,60,60,True),('linear_minus60w_extrapolation',40,.04,60,-60,True),('open_chain_plus60w_extrapolation',40,.04,60,60,False)]:
  o,t=step(parameters(power=power,cap=160e-6),n,r,delta,closed=closed);o['name']=label;steps.append(o);traces[label]=t
 results['linear_step_screen']=steps
 # Conservative charge budgets, with timing bounds explicitly assumed where TI lists typ only.
 dtmin=.765e-6/(2.33e-6*25.2)*23.89;ssmin=.55*.765/1.5
 c5=1e-3;n=40;dv5=5/ssmin;charge5=n*c5*5*dv5/.85
 main_ssmax=2.2e-6*1.265*.812/3.65e-6
 results['startup_envelope']={'main_ss_max_s_cap_temp':main_ssmax,'local_ramp_min_s':dtmin,'local_5v_ss_min_s_assumed_50pct_high_iss':ssmin,'local_5v_total_cap_assumed_max_f':c5,'40_local_precharge_max_a':.23429,'40_5v_cap_charge_peak_at_bus_w':charge5,'final_load_plus_charge_w':60+charge5,'18v_input_power_available_w':18*.9*(4.185/1.01),'5v_ss_current_not_guaranteed_min_max':True,'main_ready_margin_min_s':dtmin-(1-18.4536/24.74)*main_ssmax}
 # Worst energy envelope for abrupt disconnection/reconnection, not an arc/TVS model.
 raw=.1e-6;v=24.74;L=40e-6;I=60/23.89
 results['contact_envelope']={'raw_100nf_energy_j':.5*raw*v*v,'isolated_LC_step_undamped_peak_v':2*v,'line_energy_at_40uh_j':.5*L*I*I,'main_cap_160uf_if_it_absorbed_line_energy_v':math.sqrt(v*v+L*I*I/160e-6),'warning':'Main cap is behind reverse blocker and cannot be assumed to absorb all bus-side energy. SMBJ30A pulse/clamp + placement and contact order remain constraints.'}
 (P/'power-integrity-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 fig,ax=plt.subplots(2,2,figsize=(12,7),layout='constrained')
 for pp in [1,40,60]:
  f,db,ph=bode(parameters(power=pp,cap=160e-6),True)[1];ax[0,0].semilogx(f,db,label=f'{pp} W');ax[0,1].semilogx(f,ph)
 ax[0,0].axhline(0,color='grey',lw=.6);ax[0,0].set(xlim=(10,300000),ylim=(-50,60),title='Main loop, CPL extension, 160 uF',ylabel='Loop gain (dB)');ax[0,0].legend()
 ax[0,1].set(xlim=(10,300000),ylim=(-270,0),title='Main-loop phase',ylabel='Phase (degrees)')
 for label,(t,y)in traces.items():
  if label=='linear_minus60w_extrapolation':continue
  ax[1,0].plot(t*1000,y[0],label=label);ax[1,1].plot(t*1000,np.min(y,axis=0),label=label)
 ax[1,0].set(xlim=(0,6),title='Linear source response (screening)',xlabel='Time (ms)',ylabel='Voltage change (V)');ax[1,0].legend(fontsize=8)
 ax[1,1].set(xlim=(0,60),title='Worst bus node, linear screening',xlabel='Time (ms)',ylabel='Voltage change (V)')
 for a in ax.flat:a.grid(alpha=.25)
 fig.savefig(P/'power-integrity-plots.png',dpi=150)
 print(json.dumps({k:v for k,v in results.items()if k not in ['dc_cases','loop_scan']},indent=2))
 print('worst_pm',results['loop_scan']['worst_phase']['crossings_hz_deg'],'gm',results['loop_scan']['worst_gain_margin']['gain_margins_db'])
if __name__=='__main__':run()
