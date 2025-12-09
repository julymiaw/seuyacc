int a;

void interuptServer0(void)
{
     int b;
     int c;
     b = a&0xf000;
     c = a + 1;
     c = c & 0x0fff;
     a = ~(~b&~c);
}

void interuptServer1(void)
{
     int b;
     int c;
     b=a&0x0fff;
     c = $0xfffffc10;
     c = c<<12;
     a = ~(~b&~c);
}

void main(void)
{
     $0xfffffc20 = 2;
     $0xff22 = 0x000fffff;
     a = 0;
     while(1)
    {
         $0xff00 = a;
    }
}