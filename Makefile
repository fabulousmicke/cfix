CC	=gcc					
CCFLAGS=-g -c -Wall -O6				
LDFLAGS=					
SOURCES=cfix.c m2.c cfix_main.c					
OBJECTS=$(SOURCES:.c=.o)			
PROGRAM=cfix
					
all: $(SOURCES) $(PROGRAM)			
$(PROGRAM): $(OBJECTS)				
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ -lm
clean:						
	-rm -rf $(OBJECTS) cfix
			
.c.o:						
	$(CC) $(CCFLAGS) $< -o $@ 
