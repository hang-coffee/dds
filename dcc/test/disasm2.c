#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static const char* opnames[] = {
"LET","MOV","XCHG","LR","ST","ZERO","ADD","SUB","MUL","DIV","DIV_QWORD","CSI","CDI",
"SHL","SHR","MSL","MSR","AND","OR","XOR","NEG","MNE","PUSH","POP","SFA","RER",
"PUSHR","POPR","SRA","SRB","LOD","STO","SR","TEST","CMP","JMP","JZ","JNZ","JRZ","JRNZ","JA","JNA","JB","JNB","JG","JNG","JL","JNL",
"IN","OUT","INT","PUSH_RIN1","PUSH_RIN2","POP_RIN1","POP_RIN2","PUSHI","POPI","HLT",
"BLKS","PUSH_P","NOP","INC","DEC","BLKIN","SVC","IRET","SETB","GETB","POR"};
int main(int argc, char**argv){
	FILE*f=fopen(argv[1],"rb"); if(!f){perror("open");return 1;}
	fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
	uint8_t*b=malloc(sz); fread(b,1,sz,f); fclose(f);
	for(long i=0;i<sz;){
		uint8_t b0=b[i];
		int len=2+(b0&0xf);
		if(i+len>sz) len=sz-i;
		uint8_t op=b[i+1]&0x7f;
		uint8_t szf=(b0>>5)&3;
		const char* szn = szf==0?"BYTE":szf==1?"WORD":"DWORD";
		printf("%04lX: %-9s %-5s  ", i, op<72?opnames[op]:"???", szf==0?"-":szn);
		for(int j=0;j<len;j++) printf(" %02X", b[i+j]);
		printf("\n");
		i+=len;
	}
	return 0;
}
