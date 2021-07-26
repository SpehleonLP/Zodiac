
interface iTrain
{
	string get_name();
	void OnStartRoute();
	void OnEndRoute();
	bool TravelDistance(float length);
};

//test loading primitives and POD
class Train
{

	Train(string &in _name, int _passengers, int _year, float _speed, datetime &in _purchased)
	{
		model = _name;
		passengers = _passengers;
		year = _year;
		speed = _speed;
		purchased = _purchased;
	}
	

	string   model;
	int    	 passengers;
	int      year;
	float	 speed;
	datetime purchased;
	
	void Print()
	{
		Println("\t \"model\" : \"", model , "\"");
		Println("\t \"passengers\" : ", passengers);
		Println("\t \"year\" : ", year);
		Println("\t \"speed\" : ", speed);
		Println("\t \"purchased\" : " , purchased.year);
	}	
};

//test loading sub classes
class ElectricTrain : Train, iTrain
{
	ElectricTrain(string &in _name, int _passengers, int _year, float _speed, datetime &in _purchased, float efficiency)
	{
		super(_name, _passengers, _year, _speed, _purchased);
		energyEfficiency = efficiency;
	}
	
	float  charge;
	float  energyEfficiency;
	
	string get_name() { return model; }
	void OnStartRoute() { charge = 1000.0; }
	void OnEndRoute() {}
	
	bool TravelDistance(float length)
	{
		float time = speed / length;
		sleep(int(time*1000));
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

class DeiselTrain : Train, iTrain
{
	DeiselTrain(string _name, int _passengers, int _year, float _speed, datetime _purchased, float efficiency)
	{
		super(_name, _passengers, _year, _speed, _purchased);
		engineEfficiency = efficiency;
		testString = "testString";
	}
	
	string testString;
	float  engineEfficiency;
	float  fuel;
	
	string get_name() { return model; }
	
	void OnStartRoute() { fuel = 1000.0; }
	void OnEndRoute() {}
	
	bool TravelDistance(float length)
	{
		float time = speed / length;
		sleep(int(time*1000));
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
		name    = _name;
		@tracks = _tracks;
	
		if(allStations.exists(name))
			Println("duplicate station exists: ", name);
			
		@allStations[name] = this;
	}
	
	//test loading handles
	string	    name;
	dictionary@ tracks;
	
	float GetDistanceTo(Station@ to)
	{
		float length = -1;
		if(tracks.exists(to.name))
			tracks.get(to.name, length);
		
		return length;
	}
	
	void PrintRoutes()
	{
		string[]@ keys = tracks.getKeys();
		
		for(uint j = 0; j < keys.size(); ++j)
		{
			float length = 0;
			tracks.get(keys[j], length);
			Println("from ", name, " station to ", keys[j], " station; takes ", length , " seconds");
		}
	}
};

void PrintStations()
{
	string[]@ keys = allStations.getKeys();
	
	for(uint j = 0; j < keys.size(); ++j)
	{
		Station@ station = null;
		allStations.get(keys[j], @station);
		station.PrintRoutes();
	}
}

class TrainNode
{
	iTrain@ train;
	TrainNode@  next;
};

class HomeOffice : Station
{
	HomeOffice(string &in _name, dictionary@ _tracks) { super(_name, _tracks); }
		
	TrainNode@ 		 list;
	weakref<Station>[][] routes;
	
	void AddRoute(string[] stations)
	{
		weakref<Station>[] route;
		
		for(uint i = 0; i < stations.size(); ++i)
		{
			Station@ station;
			allStations.get(stations[i], @station);
			
			if(station is null)
			{
				Println("no such station: ", stations[i]);
			}
			
			route.push_back(weakref<Station>(@station));
		}
		
		routes.push_back(route);	
	}
	
	void PushTrain(iTrain@ train)
	{
		if(train is null)
			return;
			
		TrainNode@ n = TrainNode();
		
		@n.train = train;
		@n.next  = list;
		
		@list = n;
	}
	
	iTrain@ PopTrain()
	{
		if(list is null)
			return null;
		
		iTrain@ train = list.train;
		@list = list.next;
		
		return train;	
	}
	
	
	void FollowRoute(dictionary@ dict)
	{
		int64 id; 
		
		if(!dict.get("id", id))
		{
			Println("no 'id' key in dictionary");
			return;
		}
		
		if(uint(id) > routes.size()) 
		{
			Println("Invalid route id: ", id);
			return;
		}
					
		iTrain@ train = PopTrain();
		if(train is null) 
		{
			Println("Unable to fetch train");
			return;
		}
		
		train.OnStartRoute();
		bool r = FollowRouteInternal(@routes[id], @train, id);
		train.OnEndRoute();
		
		PushTrain(train);
		return;
	}
	
private bool FollowRouteInternal(weakref<Station>[]@ route, iTrain@ train, int route_id)
	{
		Station@ cur = this;
		Station@ next = null;
		for(uint i = 0; i < route.size(); ++i)
		{
			Println(train.get_name(), " arrived at ", cur.name, " station!");

			if((@next = route[i].get()) is null)
			{
				Println("next station missing!");
				return false;		
			}
			
			
			float length = cur.GetDistanceTo(next);
			
			if(length == 0)
			{
				Println("no route from ", cur.name, " station to ", next.name, " station!");
				return false;
			}
			else	
			{
				if(!train.TravelDistance(length))
				{
					Println(train.get_name(), " failed to reach ", next.name, " station!");
					return false;
				}
			}
			
			@cur = next;
		}
	
		return true;
	}

	void PrintRoutes()
	{
		for(uint i = 0; i < routes.size(); ++i)
		{
			Print("[");
			
			for(uint j = 0; j < routes[i].size(); ++j)
			{
				Print(routes[i][j].get().name, j+1 < routes[i].size()? ", " : "]\n");
			}
		}
	}
	
};

	class Leaf
	{
		string content;
	};

	class RingListNode
	{
		RingListNode() {}
		
		Leaf left;
		Leaf@ right;
	};
	
//test ownr load order
class RingListBuffer
{
	RingListBuffer(string[] contents)
	{
		nodes.resize(contents.size());
		
		for(uint i = 0; i < nodes.size(); ++i)
		{
			nodes[i].left.content = contents[i]; 
			@nodes[i].right 	  = nodes[(i+1) % contents.size()].left;
		}
	}
	
	void Print()
	{
		::Print("[ ");
		
		for(uint i = 0; i < nodes.size(); ++i)
		{
			::Print(nodes[i].left.content, i+1 == nodes.size()? "]\n" : ", ");		
		}	
	}
	
	bool Check()
	{
		for(uint i = 0; i < nodes.size(); ++i)
		{
			if(nodes[i].right !is nodes[(i+1) % nodes.size()].left)
				return false;
		}
	
		return true;
	}

	RingListNode[] nodes;
}


HomeOffice@ office;
Train@     antiqueTrain;
int		   test_primitive_loading = 23;
//RingListBuffer buffer({ "apple", "orange", "banana", "nectarine", "plum", "apricot" });

class Route
{
	Route() {}
	Route(float _t, int i) { time = _t; id = i; }
	float   time;
	int 	id;
};

Route[]	   schedule;

void RunSchedule()
{		
	float time = 0; 
	
	for(uint i = 0; i < schedule.size(); ++i)
	{		
		coroutine @co = coroutine(office.FollowRoute);
		createCoRoutine(@co, {{"id", schedule[i].id}});
		
		
		int sleep_time = int(schedule[i].time);
	//	Println("sleeping for ", sleep_time, " seconds");
		sleep(sleep_time < 0? sleep_time*1000 : 0);
		
		time += schedule[i].time;
	}
}


void SetUp()
{
//(string &in _name, int _passengers, int _year, float _speed, datetime &in _purchased, float efficiency)
	@antiqueTrain = Train("Smethwick", 7, 1779, 15, datetime(1779, 5, 0));
	
	@office = HomeOffice("london", { 
		{"cambridge",  5}, {"harwich",  7}, {"dover",  7}, {"brighton",  5}, {"nottingham",  13},  
		{"portsmouth",  5}, {"exter",  10},  {"cardiff",  10},  {"birmingham",  6},
		{"crewe",  14}, {"leeds",  14},  {"york",  14}
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
	Station("manchester", { {"birmingham", 7}, {"liverpool", 2}, {"leeds", 2}, {"newcastle", 9}, {"edinburgh", 13}, {"glasgow", 12}});
	
	Station("liverpool", { {"crewe",  2}, {"manchester", 2}, {"newcastle", 10}, {"edinburgh", 14}, {"glasgow", 13}});	
	Station("newcastle", { {"york",  3}, {"edinburgh", 4}, {"glasgow", 6}, {"liverpool", 9}, {"manchester", 9} });
	Station("edinburgh", { {"aberdeen",  6}, {"iverness", 10},  {"newcastle", 4}, {"glasgow", 5}, {"liverpool", 13}, {"manchester", 13} });
	Station("glasgow", { {"fort william",  6}, {"edinburgh", 4}, {"newcastle", 6}, {"liverpool", 12}, {"manchester", 12} });
	
	Station("aberdeen", { {"edinburgh",  9}, {"iverness", 14}, {"glasgow", 12}});	
	Station("iverness", { {"edinburgh",  9}, {"aberdeen", 14}, {"glasgow", 12}});	
	Station("fort william", { {"glasgow", 6}});
	
//	PrintStations();
	
	office.PushTrain(ElectricTrain("Bullet1", 200, 2000, 60, datetime(2000, 5, 12), .80));
	office.PushTrain(ElectricTrain("Bullet2", 150, 2005, 80, datetime(2005, 8, 15), .85));
	office.PushTrain(ElectricTrain("Bullet3", 240, 2010, 90, datetime(2010, 3, 30), .90));
	
	office.PushTrain(DeiselTrain("Deisel1", 400, 1995, 60, datetime(1995, 4, 13), .40));
	office.PushTrain(DeiselTrain("Deisel3", 300, 1985, 35, datetime(1989, 7, 09), .50));
	office.PushTrain(DeiselTrain("Deisel2", 300, 1995, 45, datetime(1999, 6, 24), .60));
	
	office.PushTrain(ElectricTrain("Bullet1", 200, 2000, 60, datetime(2000, 5, 12), .80));
	office.PushTrain(ElectricTrain("Bullet2", 150, 2005, 80, datetime(2005, 8, 15), .85));
	office.PushTrain(ElectricTrain("Bullet3", 240, 2010, 90, datetime(2010, 3, 30), .90));
	
	office.PushTrain(DeiselTrain("Deisel1", 400, 1995, 60, datetime(1995, 4, 13), .40));
	office.PushTrain(DeiselTrain("Deisel3", 300, 1985, 35, datetime(1989, 7, 09), .50));
	office.PushTrain(DeiselTrain("Deisel2", 300, 1995, 45, datetime(1999, 6, 24), .60));
	
	office.AddRoute({"york", "newcastle", "edinburgh", "aberdeen", "glasgow", "manchester", "birmingham", "london"});
	office.AddRoute({"portsmouth", "exter", "birmingham", "manchester", "leeds", "london"});
	office.AddRoute({"brighton", "london", "dover", "london", "harwich", "london", "cambridge", "london"});
	office.AddRoute({"cardiff", "birmingham", "manchester", "liverpool", "crewe", "london"});
	office.AddRoute({"nottingham", "london", "cardiff", "london", "exter", "london"});
	office.AddRoute({"crewe", "liverpool", "manchester", "leeds", "york", "london"});
	office.AddRoute({"birmingham", "manchester", "glasgow", "fort william", "iverness", "edinburgh", "newcastle", "york", "leeds", "london"});
	
//	PrintStations();
	
	float time = 0;
	for(int i = 0; i < 20; ++i)
	{
		time = float((rand() % 5) + 2);
		schedule.push_back(Route(time, rand() % office.routes.size()) );
	}
}

void OnLoad()
{
//	buffer.Print();
	PrintStations();
}


