import struct,os
from PIL import Image
def load(path, bg=(242,242,242)):
    d=open(path,'rb').read()
    off=struct.unpack_from('<I',d,18)[0]; size=struct.unpack_from('<I',d,14)[0]
    b=d[off:off+size]
    hs,W,H2,planes,bpp=struct.unpack_from('<IiiHH',b)
    H=H2//2; ncol=1<<bpp
    pal=[tuple(b[hs+4*i:hs+4*i+3][::-1]) for i in range(ncol)]
    px=hs+4*ncol; rowx=((W*bpp+31)//32)*4; rowa=((W+31)//32)*4
    xor=b[px:px+rowx*H]; andm=b[px+rowx*H:px+rowx*H+rowa*H]
    im=Image.new('RGB',(W,H),bg); o=im.load()
    for y in range(H):
        for x in range(W):
            byte=xor[(H-1-y)*rowx + x*bpp//8]; v=(byte>>(4*(1-x%2)))&15
            c=pal[v]; a=(andm[(H-1-y)*rowa + x//8]>>(7-x%8))&1
            if a==0: o[x,y]=c
            elif c!=(0,0,0): o[x,y]=tuple(255-q for q in bg)
    return im
D=os.path.expanduser('~/.local/share/seven-control/cursor-previews/')
m={'white-1':408,'black-1':406,'invert-1':407,'white-2':405,'black-2':403,'invert-2':404,'white-3':402,'black-3':409,'invert-3':401}
W=Image.new('RGB',(9*40,40),'white')
for i,(n,k) in enumerate(m.items()):
    im=load(f'ac/accessibilitycpl_{k}.ico'); im.save(D+n+'.png'); W.paste(im,(i*40,0))
W.resize((720,80),Image.NEAREST).save('prev.png')
