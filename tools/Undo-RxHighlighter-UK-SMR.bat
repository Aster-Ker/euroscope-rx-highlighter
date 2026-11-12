@echo off
rem Restores every file changed by Setup-RxHighlighter-UK-SMR.bat from its
rem .rxh-backup copy and removes RxHighlighter.dll from UK\Data\Plugin.
rem Close EuroScope before running.
call "%~dp0Setup-RxHighlighter-UK-SMR.bat" undo
