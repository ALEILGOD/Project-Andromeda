import math
Rg=6371.0; Rt=Rg+60.0
SIGR=(0.0058,0.0135,0.0331); HR=8.0; SIGM=(0.004,0.004,0.004); HM=1.2; G=0.76
SIGA=(0.00065,0.001881,0.000085); AC=25.0; AW=15.0; RSC=24.0
def densR(a): return math.exp(-max(a,0.0)/HR)
def densM(a): return math.exp(-max(a,0.0)/HM)
def absorb(a):
    g=math.exp(-((a-AC)/AW)**2)
    return (SIGA[0]*g,SIGA[1]*g,SIGA[2]*g)
def ext(a):
    r=densR(a); m=densM(a); ab=absorb(a)
    return (SIGR[0]*r+SIGM[0]*m+ab[0],SIGR[1]*r+SIGM[1]*m+ab[1],SIGR[2]*r+SIGM[2]*m+ab[2])
def ray_sphere(ro,rd,c,r):
    ox=ro[0]-c[0]; oy=ro[1]-c[1]; oz=ro[2]-c[2]
    b=ox*rd[0]+oy*rd[1]+oz*rd[2]; cc=ox*ox+oy*oy+oz*oz-r*r; h=b*b-cc
    if h<0: return None
    h=math.sqrt(h); return (-b-h,-b+h)
def transmittance(p,sund):
    tg=ray_sphere(p,sund,(0,0,0),Rg)
    if tg and tg[0]>0 and sund[1]<0: return (0.0,0.0,0.0)
    tt=ray_sphere(p,sund,(0,0,0),Rt)
    if not tt or tt[1]<=0: return (1.0,1.0,1.0)
    L=tt[1]; N=24; od=[0.0,0.0,0.0]
    for i in range(N):
        s=L*(i+0.5)/N
        q=(p[0]+sund[0]*s,p[1]+sund[1]*s,p[2]+sund[2]*s)
        a=math.sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2])-Rg
        e=ext(max(a,0.0)); st=L/N
        od[0]+=e[0]*st; od[1]+=e[1]*st; od[2]+=e[2]*st
    return (math.exp(-od[0]),math.exp(-od[1]),math.exp(-od[2]))
def phR(c): return 3.0/(16.0*math.pi)*(1.0+c*c)
def phM(c):
    g=G; d=(1+g*g-2*g*c)
    return 3.0/(8.0*math.pi)*((1-g*g)*(1+c*c))/((2+g*g)*d**1.5)
def ms_source(p,sund):
    dirs=[(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]
    acc=[0.0,0.0,0.0]; GL=3.0*HR; NS=4; sl=GL/NS
    for om in dirs:
        g=[0.0,0.0,0.0]
        for i in range(NS):
            s=sl*(i+0.5)
            q=(p[0]+om[0]*s,p[1]+om[1]*s,p[2]+om[2]*s)
            rq=math.sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2])
            if rq>Rt or rq<Rg: break
            a=rq-Rg
            ct=max(-1.0,min(1.0,om[0]*sund[0]+om[1]*sund[1]+om[2]*sund[2]))
            ts=transmittance(q,sund)
            j0=(densR(a)*SIGR[0]*phR(ct)+densM(a)*SIGM[0]*phM(ct))*ts[0]
            j1=(densR(a)*SIGR[1]*phR(ct)+densM(a)*SIGM[1]*phM(ct))*ts[1]
            j2=(densR(a)*SIGR[2]*phR(ct)+densM(a)*SIGM[2]*phM(ct))*ts[2]
            e=ext(a); seg=math.exp(-(e[0]+e[1]+e[2])/3.0*s)
            g[0]+=j0*seg*sl; g[1]+=j1*seg*sl; g[2]+=j2*seg*sl
        acc[0]+=g[0]; acc[1]+=g[1]; acc[2]+=g[2]
    acc=[a/6.0 for a in acc]
    a=math.sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2])-Rg
    return ((densR(a)*SIGR[0]+densM(a)*SIGM[0])*acc[0],(densR(a)*SIGR[1]+densM(a)*SIGM[1])*acc[1],(densR(a)*SIGR[2]+densM(a)*SIGM[2])*acc[2])
