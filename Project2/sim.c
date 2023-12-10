// Abigail Poropatich
// CPSC 3300: Computer Organization
// Project 2: i860 Simulator and cache system
// 04 December 2023

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

void cache_stats( void );
void cache_init( void );
void cache_access( unsigned int address, unsigned int type );

#define MEM_SIZE_IN_WORDS 256*1024
#define LINES_PER_BANK 8
#define NUM_BANKS 8


int mem[MEM_SIZE_IN_WORDS];
unsigned int
  plru_state[NUM_BANKS],               /* current state for each set */
  valid[NUM_BANKS][LINES_PER_BANK],    /* valid bit for each line    */
  dirty[NUM_BANKS][LINES_PER_BANK],    /* dirty bit for each line    */
  tag[NUM_BANKS][LINES_PER_BANK],      /* tag bits for each line     */

  plru_bank[8] /* table for bank replacement choice based on state */

                 = { 0, 0, 1, 1, 2, 3, 2, 3 },

  next_state[32] /* table for next state based on state and bank ref */
                 /* index by 5-bit (4*state)+bank [=(state<<2)|bank] */

                                    /*  bank ref  */
                                    /* 0  1  2  3 */

                 /*         0 */  = {  6, 4, 1, 0,
                 /*         1 */       7, 5, 1, 0,
                 /*         2 */       6, 4, 3, 2,
                 /* current 3 */       7, 5, 3, 2,
                 /*  state  4 */       6, 4, 1, 0,
                 /*         5 */       7, 5, 1, 0,
                 /*         6 */       6, 4, 3, 2,
                 /*         7 */       7, 5, 3, 2  };

unsigned int
    cache_reads,  /* counter */
    cache_writes, /* counter */
    hits,         /* counter */
    misses,       /* counter */
    write_backs;  /* counter */

/* processor state, simulation state, and instruction fields    */

int reg[32]   = {0}, /* general register set, r0 is always 0    */
    xip       = 0,   /* execute instruction pointer             */
    fip       = 0,   /* fetch instruction pointer               */
    halt_flag = 0,   /* set by halt instruction                 */
    verbose   = 0,   /* governs amount of detail in output      */
    ir,              /* 32-bit instruction register             */
    op1,             /* 6-bit primary opcode in bits 31 to 27   */
    d,               /* 5-bit destination register identifier   */
    s1,              /* 5-bit source 1 register identifier      */
    s2,              /* 5-bit source 2 register identifier      */
    imm16,           /* 16-bit immediate field                  */
    scaled,          /* scaled addressing mode bit 9            */
    eff_addr,        /* 32-bit effective address                */
    cc_bit = 0,      /* condition code set                      */
    genset;          /* general offset                          */     

/* dynamic execution statistics */

int inst_fetches = 0,
    memory_reads = 0,
    memory_writes = 0,
    branches = 0,
    taken = 0;


/* load memory from stdin */

#define INPUT_WORD_LIMIT 255
void get_mem(){
  int w, count = 0;

  if( verbose > 1 ) printf( "reading words in hex from stdin:\n" );
  while( scanf( "%x", &w ) != EOF ){
    if( verbose > 1 ) printf( "  0%08x\n", w );
    if( count > INPUT_WORD_LIMIT ){
      printf( "too many words loaded\n" );
      exit( 0 );
    }
    mem[ count ] = w;
    count++;
  }
  if( verbose > 1 ) printf( "\n" );
}

void read_mem( int eff_addr, int reg_index ){
  // Access the cache with the eff_addr for a read operation indicated by 0, where "read" is zero
  cache_access(eff_addr, 0);

  int word_addr = eff_addr >> 2;
  if( verbose ) printf( "  read access at address %x\n", eff_addr );
  assert( ( word_addr >= 0 ) && ( word_addr < MEM_SIZE_IN_WORDS ) );
  reg[ reg_index ] = mem[ word_addr ];
  memory_reads++;
}

void write_mem( int eff_addr, int reg_index ){
  // Access the cache with the eff_addr for a read operation indicated by 1, and write is defined by one
  cache_access(eff_addr, 1);

  int word_addr = eff_addr >> 2;
  if( verbose ) printf( "  write access at address %x\n", eff_addr );
  assert( ( word_addr >= 0 ) && ( word_addr < MEM_SIZE_IN_WORDS ) );
  mem[ word_addr ] = reg[ reg_index ];
  memory_writes++;
}

void decode(){
  op1    = ( ir >> 26 ) & 0x3f;
  d      = ( ir >> 16 ) & 0x1f;
  s1     = ( ir >> 11 ) & 0x1f;
  s2     = ( ir >> 21 ) & 0x1f;
  genset =   ir         & 0xffff;
}

// stop
void halt(){
  if( verbose ) printf( "halt\n" );
  halt_flag = 1;
}

// load, general
void imm_ld(){ 
    eff_addr = ((reg[s1] + reg[s2]) << 16) >> 16;

    if (verbose) printf("ld.l  r%x(r%x),r%x\n", s1, s2, d);
  
    read_mem(eff_addr, d);
}

// load, immediate
void imm_ldi(){
    // sign extend genset
    if (verbose) printf("ld.l  %x(r%x),r%x\n", genset, s2, d);
    genset &= 0xFFFE;

    // add genset to s2 & store in eff_addr
    eff_addr = (short)(genset + reg[s2]);

    read_mem(eff_addr, d);
}

// store, general
void imm_st(){
  int beg16 = (ir & 0xffff) & 0xf;

  if(verbose) { printf( "st.l  r%x,%x(r%x)\n", s1, beg16, s2 ); }
  // effectively clearing the LSB
  if(((ir >> 28) & 1) == true && (ir & 1) == true){ beg16 = (beg16 | 1) - 1; }

  eff_addr = (short)(beg16 + reg[s2]);

  write_mem(eff_addr, s1);

}

// add, general, signed
void adds(){ 
    // add the two source registers & store in destination register
    int sign1 = reg[s1];
    int sign2 = reg[s2];
    int result = sign1 + sign2;
    if( verbose ) printf( "adds  r%x,r%x,r%x\n", s1, s2, d );
    reg[ d ] = result;

    // set cc_bit to two's complement of sign2
    int verify = (~reg[s1] + 1);
    cc_bit = (sign2 < verify) ? 1 : 0;
}

// add, immediate, signed
void imm_adds(){ 
    if (verbose ) printf( "adds  %x,r%x,r%x\n", genset, s2, d );

    // sign extend genset
    int16_t sign_extention = (int16_t)genset; 

    reg[d] = reg[s2] + sign_extention;  

    int testing_cc_bit = (~sign_extention) + 1;
    cc_bit = (reg[s2] < testing_cc_bit) ? 1 : 0;
}

// subtract, general, signed
void subs(){ 
    if(verbose) printf("subs  r%x,r%x,r%x\n", s1, s2, d);
    int src1_value = reg[s1];
    int src2 = reg[s2];
    int result = src1_value - src2;
    
    reg[d] = result;

    // set cc_bit to two's complement of src2
    cc_bit = (reg[s2] > reg[s1]) ? 1 : 0;

}

// subtract, immediate, signed
void imm_subs() { 
    printf("subs  %x,r%x,r%x\n", genset, s2, d); 

    // 16 bit sign extension
    int16_t sign_extended_genset = (int16_t)genset; 

    reg[d] = sign_extended_genset - reg[s2]; 
    cc_bit = (reg[s2] > sign_extended_genset) ? 1 : 0;
}

// branch
void br(){ 
  // get the 16 bits from the instruction & shift them to the right
  int displace_16_bits = ir & 0x03ffffff; 
  assert( displace_16_bits != 0 );
  if( verbose ) printf( "br    %x", displace_16_bits );
  
  displace_16_bits = (displace_16_bits << 6) >> 6;

  // add the 16 bits to the fip
  fip = fip + ( displace_16_bits << 2 );

  if( verbose ){
    if( ( displace_16_bits < 0 ) || ( displace_16_bits > 9 ) ){
        printf( " (= decimal %d)\n", displace_16_bits );
  }
    else{
      printf( "\n" );
    }
  }

  branches++;
  taken++;
}

// branch if not equal
void btne() {
    int displace_16_bits = (((ir >> 16) & 0x1f) << 11) | (ir & 0x7ff); 
    if (verbose) printf("btne  r%x,r%x,%x", s1, s2, displace_16_bits); 
    branches++;

    displace_16_bits = ((displace_16_bits << 16) >> 16);

    // if the two source registers are not equal, add the 16 bits to the fip
    if (verbose) {
        if (displace_16_bits < 0 || displace_16_bits > 9) {
            printf(" (= decimal %d)\n", displace_16_bits); 
        } else {
            printf("\n"); 
        }
    }

    if (reg[s1] != reg[s2]) 
    {
      fip += (displace_16_bits << 2);
      taken++;
    }
}

// branch if not equal, immediate
void btnei() {
    // get the 5 bits from the instruction & shift them to the right
    int shift = (ir >> 11) & 0x1f; 
    int displace_16_bits  = (((ir >> 16) & 0x1f) << 11) | (ir & 0x7ff); 
    assert(displace_16_bits  != 0); 

    branches++;
    
    if (verbose) printf("btnei %x,r%x,%x", shift, s2, displace_16_bits );
    
    // shift the 16 bits to the right
    displace_16_bits  = ((displace_16_bits  << 16) >> 16); 

    if (verbose) {
        if (displace_16_bits  < 0 || displace_16_bits  > 9) {
            printf(" (= decimal %d)\n", displace_16_bits ); 
        } else {
            printf("\n"); 
        }
    }
    if (shift != reg[s2]) {
      fip = fip + (displace_16_bits  << 2); 
      taken++;
    }
}

// branch if equal
void bte() {
    int displace_16_bits  = (((ir >> 16) & 0x1f) << 11) | (ir & 0x7ff); 
    assert(displace_16_bits  != 0);

    branches++;
    if (reg[s1] != reg[s2]) return;

    displace_16_bits  = ((displace_16_bits  << 16) >> 16); 

   
    fip += (displace_16_bits  << 2); 

    taken++;

    // get the 16 bits from the instruction & shift them to the right
    if (verbose) {
        printf("bte   r%x,r%x,%x", s1, s2, displace_16_bits ); 
        if (displace_16_bits  < 0 || displace_16_bits  > 9) {
            printf(" (= decimal %d)\n", displace_16_bits ); 
        } else {
            printf("\n"); 
        }
    }
}

// branch if equal, immediate
void btei() {
    int shift = (ir >> 11) & 0x1f;
    int displace_16_bits = (((ir >> 16) & 0x1f) << 11) | (ir & 0x7ff);

    assert(displace_16_bits != 0);

    branches++;

        if (verbose) printf("btei  %x,r%x,%x", shift, s2, displace_16_bits); 
        

    if (verbose) {
        if (displace_16_bits < 0 || displace_16_bits > 9) { 
            displace_16_bits = ((displace_16_bits << 16) >> 16); 

            printf(" (= decimal %d)\n", displace_16_bits);
        } 
        
        else printf("\n");
        
    }

    // when the register equals the shift, add the 16 bits to the fip
    if (shift == reg[s2]) {
    fip += (displace_16_bits << 2);
  
    taken++;
  }
}

// branch if carry
void bc() {
    // terminate is cc is 0
    if (cc_bit != 1) {
        branches++;
        return; 
    }

    // ir & 0x03ffffff gets the 26 bits from the instruction
    int displace_16_bits = ir & 0x03ffffff;
    assert(displace_16_bits != 0);

    displace_16_bits = ((displace_16_bits << 6) >> 6);

    if (verbose) {
        printf("bc    %x", displace_16_bits);
        printf((displace_16_bits < 0 || displace_16_bits > 9) ? " (= decimal %d)\n" : "\n", displace_16_bits);
    }

    fip += (displace_16_bits << 2);

    branches++;
    taken++;
}

// branch if not carry
void bnc() {
    branches++; 

    if (cc_bit != 0) return;

    // ir & 0x03ffffff gets the 26 bits from the instruction
    int displace_16_bits = ir & 0x03ffffff;
    assert(displace_16_bits != 0); 

    if (verbose) printf("bnc   %x", displace_16_bits);
    
    displace_16_bits = ((displace_16_bits << 6) >> 6); 

    taken++;

    
    fip += (displace_16_bits << 2); 

    if (verbose) printf((displace_16_bits < 0 || displace_16_bits > 9) ? " (= decimal %d)\n" : "\n", displace_16_bits);
    
}

// shift left
void shl() {
  
    if (verbose) printf("shl   r%x,r%x,r%x\n", s1, s2, d);
    int src1_value = reg[s1];
    int src2 = reg[s2];
    int result = src2 << src1_value;
    
    reg[d] = result;
}

// shift left, immediate
void shli() {
    if (verbose)  printf("shli  %x,r%x,r%x\n", genset, s2, d);
    
    int src2 = reg[s2];
    int result = src2 << genset;

    reg[d] = result;
}

// shift right
void shr(){

  printf( "shr   r%x,r%x,r%x\n", s1, s2, d );
    unsigned int valueToShift = reg[s2];
    unsigned int positionsToShift = reg[s1];
    unsigned int result = valueToShift >> positionsToShift;

    reg[d] = result;

}

// shift right, immediate
void shri() {
    if (verbose) printf("shri  %x,r%x,r%x\n", genset, s2, d);
    int src2 = reg[s2];
    int result = src2 >> genset;

    reg[d] = result;
}

// shift right arithmetic
void shra() {
  if (verbose) printf("shra  r%x,r%x,r%x\n", s1, s2, d);
  int src1 = reg[s1];
  int src2 = reg[s2];
  int result = src2 >> src1;
  reg[d] = result;
}

// shift right arithmetic, immediate
void shrai() {
  if (verbose) printf("shrai %x,r%x,r%x\n", genset, s2, d);
  reg[d] = reg[s2] >> genset;
}

// unknown instruction
void unknown_op(){
  printf( "unknown instruction %08x\n", ir );
  printf( " op1=%x",  op1 );
  printf( " d=%x",    d );
  printf( " s1=%x",   s1 );
  printf( " s2=%x\n", s2 );
  printf( "program terminates\n" );
  exit( -1 );
}

/* Cache stats */
void cache_init( void ){
  for (int i = 0; i < LINES_PER_BANK; i++) { plru_state[i] = 0; }
    for (int i = 0; i < LINES_PER_BANK; i++) {
        for (int j = 0; j < NUM_BANKS; j++) {
            valid[i][j] = 0;
            dirty[i][j] = 0;
            tag[i][j] = 0;
        }
    }

    cache_reads = cache_writes = hits = misses = write_backs = 0;
}

void cache_stats( void ){
  printf( "cache statistics (in decimal):\n" );
  printf( "  cache reads       = %d\n", cache_reads );
  printf( "  cache writes      = %d\n", cache_writes );
  printf( "  cache hits        = %d\n", hits );
  printf( "  cache misses      = %d\n", misses );
  printf( "  cache write backs = %d\n", write_backs );
}


/* address is byte address, type is read (=0) or write (=1) */

void cache_access( unsigned int address, unsigned int type ){

  unsigned int
    addr_tag,    /* tag bits of address                           */
    addr_index,  /* index bits of address                         */
    bank,        /* bank that hit, or bank chosen for replacement */
    os = 4,      /* # of bits for offset                          */
    id = 0x7,    /* 0111 for index                                */
    tagoff = 7;  /* # of bits for                                 */

  if( type == 0 ){
    cache_reads++;
  }else{
    cache_writes++;
  }

  // Right shift address by os and id 
  // then discard the lower order bits
  // then bitwise AND with id
  addr_index = (address >> os) & id;
  addr_tag = address >> tagoff; 

  /* check bank 0 hit */

  if( valid[0][addr_index] && (addr_tag==tag[0][addr_index]) ){
    hits++;
    bank = 0;

  /* check bank 1 hit */

  }else if( valid[1][addr_index] && (addr_tag==tag[1][addr_index]) ){
    hits++;
    bank = 1;

  /* check bank 2 hit */

  }else if( valid[2][addr_index] && (addr_tag==tag[2][addr_index]) ){
    hits++;
    bank = 2;

  /* check bank 3 hit */

  }else if( valid[3][addr_index] && (addr_tag==tag[3][addr_index]) ){
    hits++;
    bank = 3;

  /* miss - choose replacement bank */

  }else{
    misses++;

         if( !valid[0][addr_index] ) bank = 0;
    else if( !valid[1][addr_index] ) bank = 1;
    else if( !valid[2][addr_index] ) bank = 2;
    else if( !valid[3][addr_index] ) bank = 3;
    else bank = plru_bank[ plru_state[addr_index] ];

    if( valid[bank][addr_index] && dirty[bank][addr_index] ){
      write_backs++;
    }

    valid[bank][addr_index] = 1;
    dirty[bank][addr_index] = 0;
    tag[bank][addr_index] = addr_tag;
  }

  /* update replacement state for this set (i.e., index value) */

  plru_state[addr_index] = next_state[ (plru_state[addr_index]<<2) | bank ];

  /* update dirty bit on a write */

  if( type == 1 ) dirty[bank][addr_index] = 1;
}



int main( int argc, char **argv ){
  
cache_init();

if( argc > 1 ){
    if( ( argv[1][0] == '-' ) && ( argv[1][1] == 't' ) ){
      verbose = 1;
    }else if( ( argv[1][0] == '-' ) && ( argv[1][1] == 'v' ) ){
      verbose = 2;
    }else{
      printf( "usage:\n");
      printf( "  %s for just execution statistics\n", argv[0] );
      printf( "  %s -t for instruction trace\n", argv[0] );
      printf( "  %s -v for instructions, registers, and memory\n", argv[0] );
      printf( "input is read as hex 32-bit values from stdin\n" );
      exit( -1 );
    }
  }

  get_mem();

  if( verbose ) printf( "instruction trace:\n" );
  while( !halt_flag ){

    if( verbose ) printf( "at %02x, ", fip );
    ir = mem[ fip >> 2 ];
    xip = fip;
    fip = xip + 4;
    inst_fetches++;


    decode();

    switch( op1 ){
      case 0x00:        halt();     break;
      case 0x04:        imm_ld();   break;
      case 0x05:        imm_ldi();  break;
      case 0x07:        imm_st();   break;
      
      case 0x14:        btne();     break;
      case 0x15:        btnei();    break; 
      case 0x16:        bte();      break;
      case 0x17:        btei();     break;
      
      case 0x1a:        br();       break;
      case 0x1c:        bc();       break;
      case 0x1e:        bnc();      break;

      case 0x24:        adds();     break;
      case 0x25:        imm_adds(); break;
      case 0x26:        subs();     break;
      case 0x27:        imm_subs(); break;
      case 0x28:        shl();      break;
      case 0x29:        shli();     break;
      
      case 0x2a:        shr();      break;
      case 0x2b:        shri();     break;
      case 0x2e:        shra();     break;
      case 0x2f:        shrai();    break;
    }

    reg[ 0 ] = 0; 

    if( ( verbose > 1 ) || ( halt_flag && ( verbose == 1 )) ){
      for( int i = 0; i < 8 ; i++ ){
        printf( "  r%x: %08x", i , reg[ i ] );
        printf( "  r%x: %08x", i + 8 , reg[ i + 8 ] );
        printf( "  r%x: %08x", i + 16, reg[ i + 16 ] );
        printf( "  r%x: %08x\n", i + 24, reg[ i + 24 ] );
      }
      printf("  cc: %x\n", cc_bit);
    }
  }

  if( verbose ) printf( "\n" );
  printf( "execution statistics (in decimal):\n" );
  printf( "  instruction fetches = %d\n", inst_fetches );
  printf( "  data words read     = %d\n", memory_reads );
  printf( "  data words written  = %d\n", memory_writes );
  printf( "  branches executed   = %d\n", branches );
  if( taken == 0 ){
    printf( "  branches taken      = 0\n" );
  }else{
    printf( "  branches taken      = %d (%.1f%%)\n",
      taken, 100.0*((float)taken)/((float)branches) );
  }
  cache_stats();
  return 0;
}