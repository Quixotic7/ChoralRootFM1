#!/usr/bin/env python3
# Key Mode scale tables (0.36): prints each scale's chord per pitch class and the C tables of cr_engine.c
# (CR_SCALE_MASK, CR_KEYMAP). Major and Minor must equal 0.35's. Usage: python3 tools/gen_scales.py
N=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
DIM,MIN,MAJ,SUS,AUG=1,2,3,4,5
Q63,Q65,QM63,QM65,Q7B9=8,9,10,11,12
E6,EM7=1,2
DOWN,UP=1,2
def e(q,sh=0,x=0): return q|sh<<4|x<<8
TRI={(4,7):MAJ,(3,7):MIN,(3,6):DIM,(4,8):AUG}
def hept(s, odd={}):
    m=[0]*12; S=set(s)
    for i,r in enumerate(s):
        t=(s[(i+2)%7]-r)%12; f=(s[(i+4)%7]-r)%12
        m[r]=e(odd[r]) if r in odd else e(TRI[(t,f)])
    for r in range(12):
        if r in S: continue
        def ok(x): return x in S and (x+5)%12 in S and (x+7)%12 in S
        lo,hi=(r-1)%12,(r+1)%12
        if ok(lo): m[r]=e(SUS,DOWN)
        elif ok(hi): m[r]=e(SUS,UP)
        elif lo in S: m[r]=e(SUS,DOWN)
        else: m[r]=e(SUS,UP)
    return m
def mask(s): return sum(1<<p for p in s)
SC=[]  # name, notes, keymap
MAJs=[0,2,4,5,7,9,11]; MINs=[0,2,3,5,7,8,10]; DORs=[0,2,3,5,7,9,10]; MIXs=[0,2,4,5,7,9,10]
SC.append(("Major",MAJs,hept(MAJs)))
SC.append(("Minor",MINs,hept(MINs)))
SC.append(("Dorian",DORs,hept(DORs)))
SC.append(("Phrygian",[0,1,3,5,7,8,10],hept([0,1,3,5,7,8,10])))
SC.append(("Lydian",[0,2,4,6,7,9,11],hept([0,2,4,6,7,9,11])))
SC.append(("Mixolydian",MIXs,hept(MIXs)))
SC.append(("Locrian",[0,1,3,5,6,8,10],hept([0,1,3,5,6,8,10])))
SC.append(("Harm Minor",[0,2,3,5,7,8,11],hept([0,2,3,5,7,8,11])))
SC.append(("Mel Minor",[0,2,3,5,7,9,11],hept([0,2,3,5,7,9,11])))
SC.append(("Maj Pent",[0,2,4,7,9],hept(MAJs)))
SC.append(("Min Pent",[0,3,5,7,10],hept(MINs)))
b=hept(MINs); b[6]=e(DIM); SC.append(("Blues",[0,3,5,6,7,10],b))
SC.append(("Phryg Dom",[0,1,4,5,7,8,10],hept([0,1,4,5,7,8,10])))
SC.append(("Dbl Harm",[0,1,4,5,7,8,11],hept([0,1,4,5,7,8,11],{7:MAJ,11:DIM})))
SC.append(("Hirajoshi",[0,2,3,7,8],hept(MINs)))
WT=[0,2,4,6,8,10]; SC.append(("Whole Tone",WT,[e(AUG,0,EM7) if r in WT else e(AUG) for r in range(12)]))
WH=[0,2,3,5,6,8,9,11]; SC.append(("Dim W-H",WH,[e(DIM,0,E6) if r in WH else e(Q7B9) for r in range(12)]))
HW=[0,1,3,4,6,7,9,10]; SC.append(("Dim H-W",HW,[(e(MAJ,0,EM7) if r%3==0 else e(DIM,0,E6)) if r in HW else e(Q7B9) for r in range(12)]))
k=hept(MIXs); k[11]=e(DIM,0,E6); SC.append(("Bebop Dom",MIXs+[11],k))
k=hept(MAJs); k[8]=e(DIM,0,E6); SC.append(("Bebop Maj",MAJs+[8],k))
k=hept(DORs); k[4]=e(DIM,0,E6); SC.append(("Bebop Min",DORs+[4],k))
H=[0,2,4,5,7,8,9,11]; k=[e(Q7B9)]*12
for r,v in {0:e(MAJ,0,E6),4:e(Q63),7:e(Q65),9:e(MIN,0,EM7),2:e(DIM,0,E6),5:e(DIM,0,E6),8:e(DIM,0,E6),11:e(DIM,0,E6)}.items(): k[r]=v
SC.append(("6-Dim",H,k))
H=[0,2,3,5,7,8,9,11]; k=[e(Q7B9)]*12
for r,v in {0:e(MIN,0,E6),3:e(QM63),7:e(QM65),9:e(DIM,0,EM7),2:e(DIM,0,E6),5:e(DIM,0,E6),8:e(DIM,0,E6),11:e(DIM,0,E6)}.items(): k[r]=v
SC.append(("m6-Dim",H,k))
QN={1:"dim",2:"m",3:"",4:"sus",5:"+",8:"6/3",9:"6/5",10:"m6/3",11:"m6/5",12:"7b9"}
for name,s,k in SC:
    ch=[]
    for r in range(12):
        v=k[r]; q=v&15; sh=(v>>4)&3; x=v>>8; rr=(r-1)%12 if sh==1 else (r+1)%12 if sh==2 else r
        ch.append(("" if r in s else "(")+N[rr]+QN[q]+("6" if x&1 else "")+("7" if x&2 else "")+("" if r in s else ")"))
    print("%-10s"%name," ".join(ch))
print()
print("static const uint16_t CR_SCALE_MASK[CR_SCALE_COUNT] = {"+", ".join("0x%03X"%mask(s) for _,s,_ in SC)+"};")
print("static const uint16_t CR_KEYMAP[CR_SCALE_COUNT][12] = {")
for name,s,k in SC: print("    { "+", ".join("0x%03X"%v for v in k)+" },   /* "+name+" */")
print("};")
