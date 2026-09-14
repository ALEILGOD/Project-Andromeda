def sky(cam,sund,view):
    tt=ray_sphere(cam,view,(0,0,0),Rt)
    if not tt or tt[1]<=0: return (0,0,0),(0,0,0),(0,0,0),(1,1,1)
    tg=ray_sphere(cam,view,(0,0,0),Rg)
    ex=tt[1]
    if tg and tg[0]>0: ex=min(ex,tg[0])
    en=max(tt[0],0.0)
    if ex<=en: return (0,0,0),(0,0,0),(0,0,0),(1,1,1)
    N=24; SS=[0.0,0.0,0.0]; MS=[0.0,0.0,0.0]; T=[1.0,1.0,1.0]
    ct=max(-1.0,min(1.0,view[0]*sund[0]+view[1]*sund[1]+view[2]*sund[2]))
    pr=phR(ct); pm=phM(ct); sl=(ex-en)/N
    for i in range(N):
        mf=(i+0.5)/N; s=en+(ex-en)*mf*mf; cur=(ex-en)*(2*i+1)/(N*N)
        q=(cam[0]+view[0]*s,cam[1]+view[1]*s,cam[2]+view[2]*s)
        a=math.sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2])-Rg
        ts=transmittance(q,sund)
        j0=(densR(a)*SIGR[0]*pr+densM(a)*SIGM[0]*pm)*ts[0]
        j1=(densR(a)*SIGR[1]*pr+densM(a)*SIGM[1]*pm)*ts[1]
        j2=(densR(a)*SIGR[2]*pr+densM(a)*SIGM[2]*pm)*ts[2]
        m2=ms_source(q,sund)
        SS[0]+=T[0]*j0*cur; SS[1]+=T[1]*j1*cur; SS[2]+=T[2]*j2*cur
        MS[0]+=T[0]*m2[0]*cur; MS[1]+=T[1]*m2[1]*cur; MS[2]+=T[2]*m2[2]*cur
        e=ext(max(a,0.0))
        T[0]*=math.exp(-e[0]*cur); T[1]*=math.exp(-e[1]*cur); T[2]*=math.exp(-e[2]*cur)
    SS=[s*RSC for s in SS]; MS=[s*RSC for s in MS]
    return (SS[0]+MS[0],SS[1]+MS[1],SS[2]+MS[2]),tuple(SS),tuple(MS),transmittance(cam,sund)
def tonemap(c):
    return tuple(max(0.0,min(1.0,1.0-math.exp(-x*0.55))) for x in c)
CASES=[("A-NOON",90.0),("B-LOW",10.0),("C-SUNSET",0.0),("D-TWILIGHT",-5.0),("E-NIGHT",-30.0)]
cam=(0.0,Rg+0.002,0.0)
out=["ZEPHYR CPU TRANSPORT SIMULATION (Earth baseline, cam h=2m, RSC=24)"]
for name,el in CASES:
    er=math.radians(el); sund=(math.cos(er),math.sin(er),0.0); mus=math.sin(er)
    views=[("ZENITH",(0,1,0)),("HOR-SUN",(1,0,0)),("HOR-ANTI",(-1,0,0))]
    for vname,view in views:
        tot,ss,ms,ts=sky(cam,sund,view)
        lum=0.2126*tot[0]+0.7152*tot[1]+0.0722*tot[2]
        tn=tonemap(tot)
        out.append("%s el=%+.0f mu_s=%+.3f %s: TOT=(%.3f,%.3f,%.3f) lum=%.3f SS=(%.3f,%.3f,%.3f) MS=(%.3f,%.3f,%.3f) Tsun=(%.3f,%.3f,%.3f) DISP=(%.3f,%.3f,%.3f)" % (name,el,mus,vname,tot[0],tot[1],tot[2],lum,ss[0],ss[1],ss[2],ms[0],ms[1],ms[2],ts[0],ts[1],ts[2],tn[0],tn[1],tn[2]))
open(r"C:\Users\aless\Documents\Unreal Projects\Andromeda\ZephyrSim_Report.txt","w").write("\n".join(out))
W=240; H=30; rows=[]
for name,el in CASES:
    er=math.radians(el); sund=(math.cos(er),math.sin(er),0.0)
    for v in [(0,1,0),(1,0,0),(-1,0,0)]:
        tot,_,_,_=sky(cam,sund,v); tn=tonemap(tot)
        rows.append(tuple(int(x*255) for x in tn))
ppm=["P3","%d %d" % (W,H*15),"255"]
for r in rows:
    for _ in range(H):
        ppm.append(" ".join("%d %d %d" % (r[0],r[1],r[2]) for _ in range(W)))
open(r"C:\Users\aless\Documents\Unreal Projects\Andromeda\ZephyrSim_Swatches.ppm","w").write("\n".join(ppm))
print("\n".join(out))
