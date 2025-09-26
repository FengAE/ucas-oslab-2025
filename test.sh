cd ~/fengxiyi23
make clean
mkdir build
make elf
cp createimage ./build
cd build && chmod +x createimage && ./createimage --extended bootblock main && cd ..
# make run
# loadboot