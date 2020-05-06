extern int read1();
extern int read2();
extern int is_valid(int);
extern void foo(int);
extern void bar(int);

void Test2_func() {
	int str = read1();
	str = read2();
	int valid = is_valid(str);
	if (valid) {
		foo(str);
	} else {
		bar(str);
	}
	return;
}

void Test2_ff() {
	int rr = read2();
	foo(rr);
}

// %1 = alloca i32, align 4
// %2 = alloca i32, align 4
// %3 = call i32 (...) @read1()
// store i32 %3, i32* %1, align 4
// %4 = call i32 (...) @read2()
// store i32 %4, i32* %1, align 4
// %5 = load i32, i32* %1, align 4
// %6 = call i32 @is_valid(i32 %5)
// store i32 %6, i32* %2, align 4
// %7 = load i32, i32* %2, align 4
// %8 = icmp ne i32 %7, 0
// br i1 %8, label %9, label %11
// %10 = load i32, i32* %1, align 4
// call void @foo(i32 %10)
// br label %13
// %12 = load i32, i32* %1, align 4
// call void @bar(i32 %12)
// br label %13
// ret void