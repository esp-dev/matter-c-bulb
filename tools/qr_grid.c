/* Host-only QR rendering helper; Nayuki implementation is linked unchanged
 * from the pinned Matter submodule. Payload enters via stdin, never argv. */
#include <stdio.h>
#include <string.h>
#include "qrcodegen.h"
int main(void){
    char payload[256];
    if(!fgets(payload,sizeof(payload),stdin))return 1;
    payload[strcspn(payload,"\r\n")]=0;
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)],qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    if(!qrcodegen_encodeText(payload,temp,qr,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,true))return 2;
    int size=qrcodegen_getSize(qr);printf("%d\n",size);
    for(int y=0;y<size;++y){for(int x=0;x<size;++x)putchar(qrcodegen_getModule(qr,x,y)?'1':'0');putchar('\n');}
    return 0;
}
