
//test value type things
class Automotive
{
	int    	 passengers;
	int      year;
	float  	 carryingCapacity;
	string 	 manufacturer;
	string 	 model;
	datetime purchased;
};

//test some reference types
class Person
{
	string 		name;
	Automotive@ vehicle;
};

//test arrays
class Garage
{
	Automotive@[] Spaces;
};


void main()
{




}
