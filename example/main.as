
//test loading primitives and POD
class Train
{
	string   model;
	int    	 passengers;
	int      year;
	float  	 carryingCapacity;
	float	 speed;
	datetime purchased;
	
	virtual void OnStartRoute();
	virtual void OnEndRoute();
	virtual bool TravelDistance(float length);
};

//test loading sub classes
class ElectricTrain : public Train
{
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
};

class DeiselTrain : public Train
{
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
};

//test arrays/lists
class Station
{
	Station(string _name, dictionary@ _tracks)
	{
		name   @= _name;
		tracks @= _tracks;
	
		if(allStations.exists(name))
			Println("duplicate station exists: ", name);
			
		allStations[name] = weakRef<Station>(this);
	}
	
	~Station()
	{
		if(!allStations.exists(name))
			Println("station already deleted: ", name);
	
		allStations.delete(name);
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

dictionary@ allStations;


class HomeOffice : Station
{
	class Node
	{
		Train@ train;
		Node@  next;
	};
	
	Node@ 		 list;
	Station@[][] routes;
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


HomeOffice office;
Train@     antiqueTrain;
int		   test_primitive_loading = 23;

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

void main()
{




}

