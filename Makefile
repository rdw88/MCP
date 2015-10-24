
mcp:
	gcc -std=c99 -g -o mcp mcp.c
	gcc -o program1 program1.c
	gcc -o program2 program2.c


clean:
	rm mcp program1 program2
