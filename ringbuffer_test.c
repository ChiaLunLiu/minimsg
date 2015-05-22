#include "ringbuffer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#define NUM  1024
int main()
{
	ringbuffer_t * rb ;
	FILE* fp1 = fopen("tmp.in","w");
	FILE* fp  = fopen("tmp.out","w");
	rb = ringbuffer_alloc(32);
	int r;
	int c ;
	char buf[2048];
	int buf_cnt = 0;
	char s[32];
	char ti[5];
	int i;
	srand(time(NULL));
	if(rb == NULL){
		printf("rb is null\n");
		return 0;
	}
	printf("size : %d\n",sizeof(ringbuffer_t));
	/* gen random data */
	for(i = 0; i < NUM ; i++){
		unsigned cc = random();
		cc %=26;
		printf("insert %c\n",(char)cc+'a');
		fprintf(fp1,"%c",cc +'a');
	}
	fclose(fp1);
	fp1 = fopen("tmp.in","r");
	
	while(1){
		r = random()%6 + 2;
		if( fgets(s,r,fp1) == NULL)break;
		r = strlen(s);
//		printf("string : %s[%d]\n",s,r);
	//	printf("free size : %d\n",ringbuffer_freesize(rb));
	//	printf("data size: %d\n",ringbuffer_datasize(rb));
		
		memcpy(buf+buf_cnt,s,r);
		buf_cnt+=r;
again:
		c = ringbuffer_push_data(rb,s,r);
		if(c == RINGBUFFER_FAIL){
			printf("DEBUG\n");
			ringbuffer_print(rb);
			printf("end\n");
			while( !ringbuffer_is_empty(rb) ){
				ringbuffer_pop_data(rb,ti,1);
				fprintf(fp,"%c",ti[0]);
			}
			goto again;
		}
		printf("original:");
		for(i =0;i<buf_cnt;i++) printf("%c",buf[i]);
		puts("");
		ringbuffer_print(rb);
	/*	printf("insert %d bytes\n", strlen(s));
		printf("(%d,%d)\n",ringbuffer_freesize(rb),ringbuffer_datasize(rb));
		if( ringbuffer_freesize(rb)+ringbuffer_datasize(rb) != 127 ){
			fprintf(stderr,"inconsistency\n");
		}
	*/
	}
	while(!ringbuffer_is_empty(rb)){	
//		printf("free size: %d, data size: %d\n",ringbuffer_freesize(rb),ringbuffer_datasize(rb));
		ringbuffer_pop_data(rb,s,1);
		fprintf(fp,"%c",s[0]);

	}
	fprintf(stderr,"hi\n");
	ringbuffer_destroy(rb);
	fprintf(stderr,"asdf\n");
	fclose(fp1);
	fclose(fp);
	/* diff */
	fp1 = fopen("tmp.in","r");
	fp  = fopen("tmp.out","r");
	for(i = 0 ;i < NUM;i++){
		char ch1 = fgetc(fp1);
		char ch2 = fgetc(fp);
		if(ch1 != ch2){
			printf("diff (%c,%c) [%d]\n",ch1,ch2,i);
			break;
		}
	}
	fclose(fp1);
	fclose(fp);
	return 0;
}
