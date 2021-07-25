int a;
int b;
int c;
int d;

string test_string;
datetime date;

void SetUp()
{
	a = 7;
	b = 13;
	c = 12;
	d = 11;
	
	test_string = "strong bad man, and his well drawn abs";
	
	date   = datetime(1991, 08, 16);
	
	OnLoad();
}

void OnLoad()
{
	Println("a = ", a, "(should be 7)");
	Println("b = ", b, "(should be 13)");
	Println("c = ", c, "(should be 12)");
	Println("d = ", d, "(should be 11)");
	Println("string = ", test_string, "should be 'strong bad man, and his well drawn abs'");
	Println("date = ", date.year, "/", date.month, "/", date.day);
}
