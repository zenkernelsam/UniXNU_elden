@echo off
cd /d D:\Desktop\Git\Patch\Y700-Elden\vendor-pack\y700-hyper-cloud\tool\bin
mkdir D:\Desktop\Git\Patch\Y700-Elden\edl_work\state_now 2>nul
tool\Win\fh_loader.exe --port=\\.\COM3 --memoryname=ufs --sendxml=D:\Desktop\Git\Patch\Y700-Elden\edl_work\read_state.xml --mainoutputdir=D:\Desktop\Git\Patch\Y700-Elden\edl_work\state_now --noprompt
