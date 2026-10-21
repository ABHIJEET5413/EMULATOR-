$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class IconHandle{[DllImport("user32.dll")]public static extern bool DestroyIcon(IntPtr h);}'
$folder=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\resources'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$bitmap=[Drawing.Bitmap]::new(64,64);$graphics=[Drawing.Graphics]::FromImage($bitmap)
$font=[Drawing.Font]::new('Segoe UI',38,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
try{
    $graphics.Clear([Drawing.Color]::FromArgb(27,39,54));$graphics.DrawString('D',$font,[Drawing.Brushes]::Turquoise,11,7)
    $handle=$bitmap.GetHicon();$icon=[Drawing.Icon]::FromHandle($handle);$stream=[IO.File]::Create((Join-Path $folder 'droidbox.ico'))
    try{$icon.Save($stream)}finally{$stream.Dispose();$icon.Dispose();[IconHandle]::DestroyIcon($handle) | Out-Null}
}finally{$font.Dispose();$graphics.Dispose();$bitmap.Dispose()}
$bitmap=[Drawing.Bitmap]::new(164,314);$graphics=[Drawing.Graphics]::FromImage($bitmap)
$font=[Drawing.Font]::new('Segoe UI',24,[Drawing.FontStyle]::Bold,[Drawing.GraphicsUnit]::Pixel)
$small=[Drawing.Font]::new('Segoe UI',12,[Drawing.FontStyle]::Regular,[Drawing.GraphicsUnit]::Pixel)
try{$graphics.Clear([Drawing.Color]::FromArgb(27,39,54));$graphics.DrawString('DroidBox',$font,[Drawing.Brushes]::Turquoise,12,40);$graphics.DrawString("Android on`nWindows",$small,[Drawing.Brushes]::White,14,90);$bitmap.Save((Join-Path $folder 'installer-banner.bmp'),[Drawing.Imaging.ImageFormat]::Bmp)}finally{$small.Dispose();$font.Dispose();$graphics.Dispose();$bitmap.Dispose()}
