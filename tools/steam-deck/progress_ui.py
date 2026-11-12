"""Small persistent updater window. Uses X11/Xft directly; no Python GUI packages.

Steam Deck supplies Xwayland and Xft for its KDE/Gamescope sessions. The window
belongs to this process and advertises the Steam shortcut class when available.
Only public update metadata enters the state file; credentials never do.
"""
import ctypes as C
import ctypes.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import time

class Progress:
    def __init__(self, title='GTA IV QoL', source=None):
        self.root=Path(tempfile.mkdtemp(prefix='gtaiv-qol-progress-'))
        self.state=self.root/'state.json';self.data={'title':title,'phase':'Starting','percent':0,'version':'','size':'','changelog':'','done':False}
        self.process=None;self.update()
        try:
            self.process=subprocess.Popen([sys.executable,'-c',source or Path(__file__).read_text(),'--window',str(self.state)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,close_fds=True)
        except (OSError,NameError):pass
    def update(self, **values):
        if 'percent' in values:values['percent']=max(self.data.get('percent',0),values['percent'])
        self.data.update(values);temp=self.state.with_suffix('.tmp');temp.write_text(json.dumps(self.data));temp.chmod(0o600);temp.replace(self.state)
    def finish(self, message='Ready to play', error=False):
        self.update(phase=message,percent=100,done=True,error=error)
    def wait(self):
        if self.process:self.process.wait()
        import shutil
        shutil.rmtree(self.root,ignore_errors=True)
    def alive(self):return self.process is not None and self.process.poll() is None

class XColor(C.Structure):_fields_=[('pixel',C.c_ulong),('red',C.c_ushort),('green',C.c_ushort),('blue',C.c_ushort),('alpha',C.c_ushort)]
class ClassHint(C.Structure):_fields_=[('name',C.c_char_p),('cls',C.c_char_p)]
class Event(C.Union):_fields_=[('type',C.c_int),('pad',C.c_long*24)]
class ButtonEvent(C.Structure):
    _fields_=[('type',C.c_int),('serial',C.c_ulong),('send',C.c_int),('display',C.c_void_p),('window',C.c_ulong),('root',C.c_ulong),('subwindow',C.c_ulong),('time',C.c_ulong),('x',C.c_int),('y',C.c_int),('x_root',C.c_int),('y_root',C.c_int),('state',C.c_uint),('button',C.c_uint),('same_screen',C.c_int)]

def window(path):
    x=C.CDLL(ctypes_path('X11'));f=C.CDLL(ctypes_path('Xft'))
    def bind(lib,name,restype,args):
        fun=getattr(lib,name);fun.restype=restype;fun.argtypes=args;return fun
    ptr=C.c_void_p;u=C.c_ulong;i=C.c_int
    open_display=bind(x,'XOpenDisplay',ptr,[C.c_char_p]);d=open_display(None)
    if not d:raise RuntimeError('No graphical display')
    screen=bind(x,'XDefaultScreen',i,[ptr])(d)
    root=bind(x,'XRootWindow',u,[ptr,i])(d,screen)
    visual=bind(x,'XDefaultVisual',ptr,[ptr,i])(d,screen)
    cmap=bind(x,'XDefaultColormap',u,[ptr,i])(d,screen)
    create=bind(x,'XCreateSimpleWindow',u,[ptr,u,i,i,C.c_uint,C.c_uint,C.c_uint,u,u])
    win=create(d,root,40,40,960,640,0,0,0x15191f)
    bind(x,'XStoreName',i,[ptr,u,C.c_char_p])(d,win,b'GTA IV QoL')
    appid=os.environ.get('SteamAppId','');cls=('steam_app_'+appid if appid.isdigit() else 'GTAIVQoL').encode()
    hint=ClassHint(b'gtaiv-qol',cls);bind(x,'XSetClassHint',i,[ptr,u,C.POINTER(ClassHint)])(d,win,C.byref(hint))
    atom=bind(x,'XInternAtom',u,[ptr,C.c_char_p,i]);pid_atom=atom(d,b'_NET_WM_PID',0);cardinal=atom(d,b'CARDINAL',0)
    pid=C.c_ulong(os.getpid());bind(x,'XChangeProperty',i,[ptr,u,u,u,i,i,ptr,i])(d,win,pid_atom,cardinal,32,0,C.byref(pid),1)
    delete=atom(d,b'WM_DELETE_WINDOW',0);delete_value=C.c_ulong(delete)
    bind(x,'XSetWMProtocols',i,[ptr,u,C.POINTER(u),i])(d,win,C.byref(delete_value),1)
    bind(x,'XSelectInput',i,[ptr,u,C.c_long])(d,win,(1<<15)|(1<<2)|(1<<17))
    gc=bind(x,'XCreateGC',ptr,[ptr,u,u,ptr])(d,win,0,None)
    fg=bind(x,'XSetForeground',i,[ptr,ptr,u]);rect=bind(x,'XFillRectangle',i,[ptr,u,ptr,i,i,C.c_uint,C.c_uint])
    flush=bind(x,'XFlush',i,[ptr]);pending=bind(x,'XPending',i,[ptr]);next_event=bind(x,'XNextEvent',i,[ptr,C.POINTER(Event)])
    draw=bind(f,'XftDrawCreate',ptr,[ptr,u,ptr,u])(d,win,visual,cmap)
    font_open=bind(f,'XftFontOpenName',ptr,[ptr,i,C.c_char_p]);fonts=[font_open(d,screen,s) for s in (b'DejaVu Sans:size=21:weight=bold',b'DejaVu Sans:size=14',b'DejaVu Sans:size=12')]
    if not draw or not all(fonts):raise RuntimeError('Font rendering unavailable')
    color_alloc=bind(f,'XftColorAllocName',i,[ptr,ptr,u,C.c_char_p,C.POINTER(XColor)])
    colors={}
    for name,value in {'white':'#f0f1ee','muted':'#b3bdc7','accent':'#8bb968','red':'#ef685d'}.items():
        color=XColor()
        if not color_alloc(d,visual,cmap,value.encode(),C.byref(color)):raise RuntimeError('Colour allocation failed')
        colors[name]=color
    text_draw=bind(f,'XftDrawStringUtf8',None,[ptr,C.POINTER(XColor),ptr,i,i,C.c_char_p,i])
    def text(xx,yy,value,font=1,color='white'):
        raw=str(value).encode('utf-8','replace');text_draw(draw,C.byref(colors[color]),fonts[font],xx,yy,raw,len(raw))
    def box(xx,yy,w,h,color):fg(d,gc,color);rect(d,win,gc,xx,yy,w,h)
    bind(x,'XMapRaised',i,[ptr,u])(d,win);flush(d)
    expanded=False;scroll=0;last='';state={};last_paint=0
    while True:
        try:raw=Path(path).read_text();state=json.loads(raw)
        except (OSError,ValueError):raw=last
        dirty=raw!=last;last=raw
        while pending(d):
            event=Event();next_event(d,C.byref(event));dirty=True
            if event.type==33 and state.get('done'):return
            if event.type==4:
                e=C.cast(C.byref(event),C.POINTER(ButtonEvent)).contents
                if 35<=e.x<=925 and 290<=e.y<=335:expanded=not expanded;scroll=0
                elif expanded and e.button in (4,5):scroll=max(0,scroll+(-2 if e.button==4 else 2))
                elif state.get('done') and 760<=e.x<=920 and 565<=e.y<=615:return
        if dirty or time.monotonic()-last_paint>.25:
            box(0,0,960,640,0x15191f);text(36,62,state.get('title','GTA IV QoL'),0)
            text(36,112,state.get('version') or 'Checking for updates',1)
            text(36,145,state.get('size',''),2,'muted')
            for row,line in enumerate(textwrap.wrap(str(state.get('phase','Starting'))+('' if state.get('done') else '.'*(int(time.monotonic()*2)%4)),82)[:2]):text(36,190+row*25,line,1,'red' if state.get('error') else 'white')
            percent=max(0,min(100,float(state.get('percent',0))))
            box(36,230,888,16,0x333c46);box(36,230,int(888*percent/100),16,0x8bb968 if not state.get('error') else 0xef685d)
            text(860,275,str(round(percent))+'%',2,'muted')
            box(36,294,888,40,0x242b34);text(50,321,('−' if expanded else '+')+'  What changed',1)
            if expanded:
                lines=[]
                for line in str(state.get('changelog') or 'Release notes are not available for this build.').splitlines():lines+=textwrap.wrap(line,88) or ['']
                scroll=min(scroll,max(0,len(lines)-8))
                for row,line in enumerate(lines[scroll:scroll+8]):text(50,368+row*23,line,2,'muted')
                if len(lines)>8:text(50,558,'Scroll to read more',2,'muted')
            if state.get('done'):
                box(760,570,164,42,0x35482e);text(812,599,'Close',1)
            flush(d);last_paint=time.monotonic()
        time.sleep(.04)

def ctypes_path(name):
    path=C.util.find_library(name)
    if not path:raise RuntimeError(name+' library unavailable')
    return path

if __name__=='__main__' and len(sys.argv)>2 and sys.argv[1]=='--window':window(sys.argv[2])
