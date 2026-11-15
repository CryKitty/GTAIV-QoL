"""Standard-library Windows progress UI, hosted in the frozen GUI process."""
class Progress:
    def __init__(self,title='GTA IV QoL'):
        import tkinter as tk
        from tkinter import ttk
        self.root=tk.Tk();self.root.title(title);self.root.minsize(620,240)
        self.data={'phase':'Starting','percent':0};self.closed=False
        self.phase=tk.StringVar(value='Starting');self.detail=tk.StringVar()
        ttk.Label(self.root,textvariable=self.phase,wraplength=570).pack(padx=24,pady=16)
        self.bar=ttk.Progressbar(self.root,maximum=100,length=560);self.bar.pack(padx=24,pady=8)
        ttk.Label(self.root,textvariable=self.detail,wraplength=570).pack(padx=24,pady=8)
        self.notes=tk.Text(self.root,height=12,width=72,wrap='word');self.shown=False
        ttk.Button(self.root,text='Show / hide changelog',command=self.toggle).pack(pady=8)
        self.done=ttk.Button(self.root,text='Done',command=self.close)
        self.root.protocol('WM_DELETE_WINDOW',self.close);self.root.update()
    def toggle(self):
        self.shown=not self.shown
        if self.shown:self.notes.pack(padx=24,pady=8)
        else:self.notes.pack_forget()
    def update(self,**values):
        self.data.update(values)
        if self.closed:return
        self.phase.set(self.data.get('phase',''));self.bar['value']=self.data.get('percent',0)
        self.detail.set(' · '.join(str(self.data.get(k,'')) for k in ('version','size') if self.data.get(k)))
        if 'changelog' in values:self.notes.delete('1.0','end');self.notes.insert('1.0',str(values['changelog']))
        self.root.update()
    def finish(self,message='Ready to play',error=False):
        self.update(phase=message,percent=100,error=error)
        if not self.closed:self.done.pack(pady=12)
    def alive(self):return not self.closed
    def close(self):
        self.closed=True;self.root.quit();self.root.destroy()
    def wait(self):
        if not self.closed:self.root.mainloop()
