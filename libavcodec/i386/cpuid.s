	.file	"cpuid.s"
	.version	"01.01"
gcc2_compiled.:
.text
	.align 4
        .globl   cpuid
	.type	 cpuid,@function
cpuid:
	pushl %ebp
	movl %esp,%ebp
	pushl %edi
	pushl %esi
	pushl %ebx
	movl 8(%ebp),%eax
	movl 12(%ebp),%edi
	movl 24(%ebp),%esi
	cpuid
	movl %eax,(%edi)
	movl 16(%ebp),%eax
	movl %ebx,(%eax)
	movl 20(%ebp),%eax
	movl %ecx,(%eax)
	movl %edx,(%esi)
	popl %ebx
	popl %esi
	popl %edi
	leave
	ret
.Lfe1:
	.size	 cpuid,.Lfe1-cpuid
	.align 4
