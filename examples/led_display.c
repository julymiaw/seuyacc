void delay(void)
{
    int c;c=30000;
     while(c>0){ c=c-1;}
}

void main(void)
{
    int key;key=0;
    while(1)
    {
         key=key+1;
         $0xfffffc00=key;
          if(key>10) key=0;
          delay();
    }
}