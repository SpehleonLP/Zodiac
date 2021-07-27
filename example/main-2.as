int a;
int b;
int c;
int d;

class Foo
{
	Foo()
	{
		Foo(0, 0, 0, 0);
	}
	Foo(int _a, int _b, int _c, int _d)
	{
		a = _a; b = _b; c = _c; d = _d;
	}
	
	int a, b, c, d;

	void print(int _a, int _b, int _c, int _d)
	{
		Println("a = ", a, " (should be ", _a, ")");
		Println("b = ", b, " (should be ", _b, ")");
		Println("c = ", c, " (should be ", _c, ")");
		Println("d = ", d, " (should be ", _d, ")");	
	}
}

string test_string;
datetime date;

//Foo fooA;
//Foo@ fooB;
//Foo fooC;
//Foo@ fooD;
weakref<Foo> weakFoo;

array<int> _array;

funcdef void MyFunc();
MyFunc@ test_func;

dictionary dict;

void TestFunc()
{
	Println("test test test");
}


void SetUp()
{
//	fooA = Foo(1, 2, 3, 4);
//	@fooB = Foo(5, 6, 7, 8);
	//fooC =;
//	@fooD = fooC;
	
	@dict["key"] =  Foo(9, 10, 11, 12);
	//@weakFoo = fooA;
	@test_func = TestFunc;
	
	_array.resize(10);
	
	for(int i = 0; i < _array.size(); ++i)
	{
		_array[i] = i;
	}
	
	a = 7;
	b = 13;
	c = 12;
	d = 11;
	
	test_string = "strong bad man, and his well drawn abs";
	
	date   = datetime(1991, 08, 16);
	
	OnLoad();
	
	Println("finished set up");
}

void OnLoad()
{
	Println("a = ", a, " (should be 7)");
	Println("b = ", b, " (should be 13)");
	Println("c = ", c, " (should be 12)");
	Println("d = ", d, " (should be 11)");
	Println("string = '", test_string, "' should be 'strong bad man, and his well drawn abs'");
	Println("date = ", date.year, "/", date.month, "/", date.day);
	
	/*fooA.print(1, 2, 3, 4);
	fooB.print(5, 6, 7, 8);
	fooC.print(9, 10, 11, 12);
	fooD.print(9, 10, 11, 12);*/
	
	Println("array = ", _array);
	test_func();
}
