@echo off
cmake --preset wclang-debug
cmake  --build build/debug

copy config.yaml build\debug\