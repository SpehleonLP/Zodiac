
//test loading primitives and POD
class Train
{

	Train(string &in _name, int _passengers, int _year, float _speed, datetime &in _purchased, float efficiency)
	{
		name = _name;
		passengers = _passengers;
		year = _year;
		speed = _speed;
		purchased = _purchased;
		energyEfficiency = efficiency;
	}
	

	string   model;
	int    	 passengers;
	int      year;
	float	 speed;
	datetime purchased;

	virtual void OnStartRoute() {}
	virtual void OnEndRoute() {}
	virtual bool TravelDistance(float length) { return false; }
	
	virtual void Print()
	{
		Println("\t \"model\" : \"", model , "\"");
		Println("\t \"passengers\" : ", passengers);
		Println("\t \"year\" : ", year);
		Println("\t \"speed\" : ", speed);
		Println("\t \"purchased\" : " , purchased.year());
	}	
};

//test loading sub classes
class ElectricTrain : public Train
{
	ElectricTrain(string &in _name, int _passengers, int _year, float _speed, datetime &in _purchased, float efficiency)
	{
		Train(_name, _passengers, _year, _speed, _purchased);
		energyEfficiency = efficiency;
	}
	
	float  charge;
	float  energyEfficiency;
	
	void OnStartRoute() { charge = 1000.0; }
	void OnEndRoute() {}
	
	bool TravelDistance(float length)
	{
		float time = speed / length;
		sleep(time * 1000);
		charge -= (1.0 - energyEfficiency) * speed * time;
		return charge > 0;
	}
	
	void Print()
	{
		Println("{");
		Train::Print();
		Println("\t \"charge\" : " , charge);
		Println("\t \"efficiency\" : " , energyEfficiency);
		Println("}");
	}
};

class DeiselTrain : public Train
{
	ElectricTrain(string _name, int _passengers, int _year, float _speed, datetime _purchased, float efficiency)
	{
		Train(_name, _passengers, _year, _speed, _purchased);
		engineEfficiency = efficiency;
		testString = "testString";
	}
	
	string testString;
	float  engineEfficiency;
	float  fuel;
	
	void OnStartRoute() { fuel = 1000.0; }
	void OnEndRoute() {}
	
	bool TravelDistance(float length)
	{
		float time = speed / length;
		sleep(time * 1000);
		fuel -= (1.0 - engineEfficiency) * speed * time;
		return fuel > 0;
	}
	
	void Print()
	{
		Println("{");
		Train::Print();
		Println("\t \"charge\" : " , fuel);
		Println("\t \"efficiency\" : " , engineEfficiency);
		Println("}");
	}
};

//test arrays/lists
dictionary allStations;

class Station
{
	Station(string &in _name, dictionary@ _tracks)
	{
		name   @= _name;
		tracks @= _tracks;
	
		if(allStations.exists(name))
			Println("duplicate station exists: ", name);
			
		allStations[name] = this;
	}
	
	//test loading handles
	string 	   name;
	dictionary tracks;
	
	float GetDistanceTo(Station@ to)
	{
		float length = 0;
		if(tracks.exists(to.name))
			tracks.get(to.name, length);
		
		return length;
	}
	
	int PrintRoutes()
	{
		string[] keys @= tracks.getKeys();
		
		for(uint j = 0; j < keys.size(); ++j)
		{
			float length = 0;
			tracks.get(keys[i], length);
			Println("from ", name, " station to ", keys[i], "station; takes ", length , " seconds");
		}
	}
};



class HomeOffice : Station
{
	class Node
	{
		Train@ train;
		Node@  next;
	};
	
	HomeOffice(string &in _name, dictionary@ _tracks) { Station(_name, _tracks); }
		
	Node@ 		 list;
	weakref<Station>[][] routes;
	Route[]		 schedule;
	
	void PushTrain(Train@ train)
	{
		if(train is null)
			return;
			
		Node@ n = Node();
		
		n.train = train;
		@n.next  = list;
		
		@list = n;
	}
	
	Train@ PopTrain()
	{
		if(list is null)
			return null;
		
		Train@ train = list.train;
		@list = list.next;
		
		return train;	
	}
	
	
	bool FollowRoute(dictionary@ dict)
	{
		int64 id; 
		
		if(!dict.get("id", id) || (uint)id > routes.size()) 
			return false;
					
		Train@ train = PopTrain()
		if(train is null) return false;
		
		train.OnStartRoute();
		bool r = FollowRoute(@routes[id], @train);
		train.OnEndRoute();
		
		PushTrain(train);
		return r;
	}
	
	bool FollowRoute(Station[] route, Train@ train)
	{
		Station@ cur = this;
		for(uint i = 0; i < route.size(); ++i)
		{
			Println(train.name, " arrived at ", cur.name, " station!");
			
			float length = cur.GetDistanceTo(routes[i]);
			
			if(length == 0)
			{
				Println(" no route from ", cur.name, " station to ", routes[i].name, " station!");
				return false;
			}
			else	
			{
				if(!train.TravelDistance(length))
				{
					Println(train.name, " failed to reach ", routes[i].name, " station!");
					return false;
				}
			}
		}
	
		return true;
	}
	
};

//test ownr load order
class RingListBuffer
{
	RingListBuffer(string[] contents)
	{
		nodes.resize(contents.size());
		
		for(uint i = 0u; i < nodes.size(); ++i)
		{
			nodes[i].left.content = contents[i]; 
			@nodes[i].right 	  = nodes[(i+1) % contents.size()].left;
		}
	}
	
	void Print()
	{
		Print("[ ");
		
		for(uint i = 0u; i < nodes.size(); ++i)
		{
			Print(nodes[i].left.content, i+1 == nodes.size()? "]" : ", ");		
		}	
	}
	
	bool Check()
	{
		for(uint i = 0u; i < nodes.size(); ++i)
		{
			if(nodes[i].right !is nodes[(i+1) % nodes.size()].left)
				return false;
		}
	
		return true;
	}

	class Leaf
	{
		string content;
	};

	class Node
	{
		Leaf left;
		Leaf@ right;
	};
	
	Node[] nodes;
}


HomeOffice@ office;
Train@     antiqueTrain;
int		   test_primitive_loading = 23;
RingListBuffer buffer({ "apple", "orange", "banana", "nectarine", "plum", "apricot" });

class Route
{
	float       time;
	dictionary@ dict;
};

Route[]	   schedule;

void RunSchedule()
{
	float time; 
	
	for(uint i = 0; i < schedule.size(); ++i)
	{
		sleep(1000 * (schedule[i].time - time));
		time += schedule[i].time;
		
		createCoRoutine(coroutine(office.FollowRoute), schedule[i].dict);
	}
}

void SetUp()
{(string &in _name, int _passengers, int _year, float _speed, datetime &in _purchased, float efficiency)
	antiqueTrain = Train("Smethwick", 7, 1779, 15, datetime(1779, 5, 0), .15);
	
	office = HomeOffice("london", { 
		{"cambridge",  5}, {"harwich",  7}, {"dover",  7}, {"brighton",  5}, {"nottingham",  13},  
		{"portsmouth",  5}, {"exter",  10},  {"cardiff",  10},  {"birmingham",  6},
		{"crewe",  14}, {"leeds",  14},  {"york",  14},
	});
		
	Station("cambridge", { {"london",  5} });
	Station("harwich", { {"london",  8} });
	Station("dover", { {"london",  7} });
	Station("brighton", { {"london",  5} });
	Station("nottingham", { {"london",  13} });
	
	Station("portsmouth", { {"london",  5}, {"exter", 8}, {"cardiff", 10}, {"birmingham", 13}});
	Station("exter", { {"london",  10}, {"cardiff", 8}, {"portsmouth", 10}, {"birmingham", 12}});
	Station("cardiff", { {"london",  10}, {"exter", 8}, {"portsmouth", 10}, {"birmingham", 12}});
	Station("birmingham", { {"london",  6}, {"exter", 8}, {"portsmouth", 10}, {"cardif", 12}});
	
	Station("crewe", { {"london",  14}, {"liverpool", 2}});
	Station("leeds", { {"london",  14}, {"manchester", 2}, {"york", 1}});
	Station("york", { {"london",  14}, {"leeds", 1}, {"newcastle", 3}});
	
	Station("liverpool", { {"crewe",  2}, {"manchester", 2}, {"newcastle", 10}, {"edinburgh", 14}, {"glasgow", 13}});	
	Station("newcastle", { {"york",  3}, {"edinburgh", 4}, {"glasgow", 6}, {"liverpool", 9}, {"manchester", 9} });
	Station("edinburgh", { {"aberdeen",  6}, {"iverness", 10},  {"newcastle", 4}, {"glasgow", 5}, {"liverpool", 13}, {"manchester", 13} });
	Station("glasgow", { {"fort william",  6}, {"edinburgh", 4}, {"newcastle", 6}, {"liverpool", 12}, {"manchester", 12} });
	
	Station("aberdeen", { {"edinburgh",  9}, {"iverness", 14}, {"glasgow", 12}});	
	Station("iverness", { {"edinburgh",  9}, {"aberdeen", 14}, {"glasgow", 12}});	
	Station("fort william", {"glasgow", 6}});
	


}

void Destroy()
{
	@buffer = null;
	allStations.clear();


}

