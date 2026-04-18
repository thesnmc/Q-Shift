all: kernel user

kernel:
	clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I/usr/include/x86_64-linux-gnu -c qshift_kern.c -o qshift_kern.o

user:
	clang -O2 -g -o qshift_user qshift_user.c -lbpf -lelf -lxdp -lcurl -loqs -lcrypto

clean:
	rm -f qshift_kern.o qshift_user
