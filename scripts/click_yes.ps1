#requires -Version 5.1
# 自动点掉 FA2sp 的单实例互斥对话框（"FA2sp Init Checker" -> 是）
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class YesClicker {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public static int Click(string title) {
        IntPtr h = FindWindow(null, title);
        if (h == IntPtr.Zero) return 0;
        SendMessage(h, 0x0111, (IntPtr)6, IntPtr.Zero);
        return 1;
    }
}
'@ -Language CSharp

$hit = 0
for ($i = 0; $i -lt 10; $i++) {
    if ([YesClicker]::Click('FA2sp Init Checker') -eq 1) { $hit++ }
    Start-Sleep -Milliseconds 800
}
Write-Host ("clicked {0} time(s)" -f $hit)
