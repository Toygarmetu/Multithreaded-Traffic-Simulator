#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <vector>
#include <semaphore.h>
#include <queue>

#include "WriteOutput.h"
#include "helper.h"
#include "monitor.h"

using namespace std;

void *narrow_bridge_revers_timer(void *narrow_bridge_id);
void *ferry_timer_func(void *ferry_id);
void *crossroad_timer_func(void *crossroad_id);

struct RC
{
    int ID;
    int travelTime;
    int maxWaitTime;
};

class Car
{
public:
    int id;
    int travelTime;
    int pathLength;
    vector<int> RCType;
    vector<int> RCId;
    vector<int> from;
    vector<int> to;
    sem_t wait_passing;
    Car(int id, int travelTime, int pathLength)
    {
        this->id = id;
        this->travelTime = travelTime;
        this->pathLength = pathLength;
        sem_init(&wait_passing, 0, 0);
    }
};

typedef struct PassingCar
{
    Car *car;
    int from;
    int to;
    PassingCar(Car *car, int from, int to)
    {
        this->car = car;
        this->from = from;
        this->to = to;
    }
} PassingCar;





class NarrowBridge : public Monitor
{
public:
    int maxWaitTime;
    int id;
    int travelTime;
    queue<Car *> carLeftQueue;
    queue<Car *> carRightQueue;
    queue<PassingCar> carQueue;
    Condition syncCondBridge;
    sem_t notifications;
    char direction;
    int on_bridge;
    bool is_end;
    bool is_blocked_till_reverse;
    bool should_join_timer;
    bool is_timer_started = false;
    pthread_t timer;

    NarrowBridge(int id, int maxWaitTime, int travelTime) : syncCondBridge(this)
    {
        this->maxWaitTime = maxWaitTime;
        this->id = id;
        this->travelTime = travelTime;
        this->direction = 'L';
        this->on_bridge = 0;
        this->is_end = false;
        this->is_blocked_till_reverse = false;
        this->should_join_timer = false;
        sem_init(&notifications, 0, 1);
    }

    void block_bridge()
    {
        __synchronized__;
        is_blocked_till_reverse = true;
        is_timer_started = false;
        sem_post(&notifications);
    }

    void reverse_bridge()
    {
        __synchronized__;

        is_blocked_till_reverse = false;
        is_timer_started = false;
        if (direction == 'L')
        {
            direction = 'R';
        }
        else
        {
            direction = 'L';
        }
    }

    bool get_should_join_timer()
    {
        __synchronized__;
        return should_join_timer;
    }

    void join_timer()
    {
        __synchronized__;
        pthread_join(timer, NULL);
        should_join_timer = false;
    }

    void start_timer()
    {
        __synchronized__;
        is_timer_started = true;
        pthread_create(&timer, NULL, narrow_bridge_revers_timer, (void *)&id);
    }

    bool get_is_timer_started()
    {
        __synchronized__;
        return is_timer_started;
    }

    void set_should_join_timer()
    {
        __synchronized__;
        should_join_timer = true;
        sem_post(&notifications);
    }

    void enter_bridge(Car *car, int from, int to)
    {
        __synchronized__;
        PassingCar passing_car(car, from, to);
        carQueue.push(passing_car);
        sem_post(&notifications);
    }

    void finish_passing()
    {
        __synchronized__;
        on_bridge -= 1;
        sem_post(&notifications);
    }

    void start_passing()
    {
        __synchronized__;
        on_bridge += 1;
    }

    int get_num_on_bridge()
    {
        __synchronized__;
        return on_bridge;
    }

    bool get_is_blocked()
    {
        __synchronized__;
        return is_blocked_till_reverse;
    }

    char get_direction()
    {
        __synchronized__;
        return direction;
    }

    bool is_car_queue_enmpty()
    {
        __synchronized__;
        return carQueue.empty();
    }

    PassingCar get_passing_car()
    {
        __synchronized__;
        PassingCar passing_car = carQueue.front();
        carQueue.pop();
        return passing_car;
    }

    void operate_bridge()
    {

        while (!is_end)
        {
            if (!is_car_queue_enmpty())
            {
                PassingCar passing_car = get_passing_car();
                if (passing_car.to)
                {
                    if (carLeftQueue.empty() && get_direction() == 'R' && !get_is_blocked())
                    {
                        // First car to come to left bridge
                        // start timer thread
                        start_timer();
                    }

                    carLeftQueue.push(passing_car.car);
                }
                else
                {
                    if (carRightQueue.empty() && get_direction() == 'L' && !get_is_blocked())
                    {
                        // First car to come to left bridge
                        // start timer thread
                        start_timer();
                    }

                    carRightQueue.push(passing_car.car);
                }
            }
            else if (!carLeftQueue.empty() && get_direction() == 'L' && !get_is_blocked())
            {
                Car *car = carLeftQueue.front();
                carLeftQueue.pop();
                sem_post(&car->wait_passing);
                sleep_milli(PASS_DELAY);
            }
            else if (!carRightQueue.empty() && get_direction() == 'R' && !get_is_blocked())
            {
                Car *car = carRightQueue.front();
                carRightQueue.pop();
                sem_post(&car->wait_passing);
                sleep_milli(PASS_DELAY);
            }
            else if (get_is_blocked() && get_num_on_bridge() == 0)
            {
                reverse_bridge();
            }
            else if (get_should_join_timer())
            {
                join_timer();
            }
            else if (get_is_timer_started() && get_num_on_bridge() == 0)
            {
                block_bridge();
                reverse_bridge();
            }
            else
            {
                sem_wait(&notifications);
            }
        }
    }
};

class Ferry : public Monitor
{

    Condition wait_movement_right;
    Condition wait_movement_left;
    Condition wait_entring_left;
    Condition wait_entring_right;
    int on_ferry_left;
    int on_ferry_right;
    bool left_is_moving;
    bool right_is_moving;

public:
    int id;
    int capacity;

    unsigned long travel_time;
    unsigned long max_wait_time;
    Ferry(int ferry_id, int ferry_capacity, unsigned long ferry_travel_time, unsigned long ferry_max_wait_time) : wait_movement_right(this), wait_movement_left(this),
                                                                                                                  wait_entring_left(this), wait_entring_right(this)
    {
        id = ferry_id;
        capacity = ferry_capacity;
        travel_time = ferry_travel_time;
        max_wait_time = ferry_max_wait_time;
        left_is_moving = false;
        right_is_moving = false;
        on_ferry_left = 0;
        on_ferry_right = 0;
    }

    void cross_ferry(int car_id, int to)
    {
        __synchronized__;

        if (to)
        {
            while (left_is_moving)
            {
                wait_entring_left.wait();
            }
            if (on_ferry_left == 0)
            {
                on_ferry_left += 1;

                struct timeval now;
                struct timespec ts;
                gettimeofday(&now, NULL);
                ts.tv_nsec = now.tv_usec * 1000 + max_wait_time * (1000 * 1000);
                ts.tv_sec = now.tv_sec + ts.tv_nsec / 1000000000;
                ts.tv_nsec %= (1000 * 1000 * 1000);

                wait_movement_left.timedwait(&ts);

                left_is_moving = true;
                wait_movement_left.notifyAll();
                on_ferry_left -= 1;

                if (on_ferry_left == 0)
                {
                    left_is_moving = false;
                    wait_entring_left.notifyAll();
                }
            }
            else if (on_ferry_left < capacity)
            {
                on_ferry_left += 1;
                if (on_ferry_left == capacity)
                {
                    wait_movement_left.notifyAll();
                }
                else
                {
                    wait_movement_left.wait();
                }
                left_is_moving = true;
                on_ferry_left -= 1;
                if (on_ferry_left == 0)
                {
                    left_is_moving = false;
                    wait_entring_left.notifyAll();
                }
            }
        }
        else
        {
            while (right_is_moving)
            {
                wait_entring_right.wait();
            }

            if (on_ferry_right == 0)
            {
                on_ferry_right += 1;

                struct timeval now;
                struct timespec ts;
                gettimeofday(&now, NULL);
                ts.tv_nsec = now.tv_usec * 1000 + max_wait_time * (1000 * 1000);
                ts.tv_sec = now.tv_sec + ts.tv_nsec / 1000000000;
                ts.tv_nsec %= (1000 * 1000 * 1000);

                wait_movement_right.timedwait(&ts);

                right_is_moving = true;
                wait_movement_right.notifyAll();
                on_ferry_right -= 1;

                if (on_ferry_right == 0)
                {
                    right_is_moving = false;
                    wait_entring_right.notifyAll();
                }
            }
            else if (on_ferry_right < capacity)
            {
                on_ferry_right += 1;
                if (on_ferry_right == capacity)
                {
                    wait_movement_right.notifyAll();
                }
                else
                {
                    wait_movement_right.wait();
                }
                right_is_moving = true;
                on_ferry_right -= 1;
                if (on_ferry_right == 0)
                {
                    right_is_moving = false;
                    wait_entring_right.notifyAll();
                }
            }
        }
    }
};



class Crossroad : public Monitor
{
public:
    int ID;
    int travelTime;
    int maxWaitTime;

    queue<Car *> carLeftQueue;
    queue<Car *> carRightQueue;
    queue<Car *> carUpQueue;
    queue<Car *> carDownQueue;
    queue<PassingCar> carQueue;

    Condition syncCondUp;
    Condition syncCondDown;

    sem_t notifications;
    char direction;
    int on_cross;
    bool is_end;
    bool is_blocked_till_reverse;
    bool should_join_timer;
    bool is_timer_started = false;
    pthread_t timer;



    Crossroad(int ID, int travelTime, int maxWaitTime) : syncCondUp(this), syncCondDown(this)
    {
        this->ID = ID;
        this->travelTime = travelTime;
        this->maxWaitTime = maxWaitTime;

        this->direction = 'L';
        this->on_cross = 0;
        this->is_end = false;
        this->is_blocked_till_reverse = false;
        this->should_join_timer = false;
        sem_init(&notifications, 0, 1);
    }

    void block_crossroad()
    {
        __synchronized__;
        is_blocked_till_reverse = true;
        is_timer_started = false;
        sem_post(&notifications);
    }

    void update_crossroad()
    {
        __synchronized__;
        is_blocked_till_reverse = false;
        is_timer_started = false;
       if (direction == 'L')
        {
            direction = 'D';
        }
        else if (direction == 'D')
        {
            direction = 'R';
        }
        else if (direction == 'R')
        {
            direction = 'U';
        }
        else
        {
            direction = 'L';
        }
        
    }


    bool get_should_join_timer()
    {
        __synchronized__;
        return should_join_timer;
    }

    void join_timer()
    {
        __synchronized__;
        pthread_join(timer, NULL);
        should_join_timer = false;
    }

    void start_timer()
    {
        __synchronized__;
        is_timer_started = true;
        pthread_create(&timer, NULL, crossroad_timer_func, (void *)&ID);
    }

    bool get_is_timer_started()
    {
        __synchronized__;
        return is_timer_started;
    }

    void set_should_join_timer()
    {
        __synchronized__;
        should_join_timer = true;
        sem_post(&notifications);
    }

    void enter_crossroad(Car *car, int from, int to)
    {
        __synchronized__;
        PassingCar passing_car(car, from, to);
        carQueue.push(passing_car);
        sem_post(&notifications);
    }

    void finish_passing()
    {
        __synchronized__;
        on_cross -= 1;
        sem_post(&notifications);
    }
    
    void start_passing()
    {
        __synchronized__;
        on_cross += 1;
    }

    int get_num_on_cross()
    {
        __synchronized__;
        return on_cross;
    }

    bool get_is_blocked()
    {
        __synchronized__;
        return is_blocked_till_reverse;
    }

    char get_direction()
    {
        __synchronized__;
        return direction;
    }

    bool is_car_queue_enmpty()
    {
        __synchronized__;
        return carQueue.empty();
    }

    PassingCar get_passing_car()
    {
        __synchronized__;
        PassingCar passing_car = carQueue.front();
        carQueue.pop();
        return passing_car;
    }



   void operate_crossroad()
    {

        while (!is_end)
        {
            if (!is_car_queue_enmpty())
            {
                PassingCar passing_car = get_passing_car();
                if (passing_car.to)
                {
                    if (carLeftQueue.empty() && get_direction() == 'R' && !get_is_blocked())
                    {
                        // First car to come to left bridge
                        // start timer thread
                        start_timer();
                    }

                    carLeftQueue.push(passing_car.car);
                }
               else if (passing_car.to)
                {
                    if (carRightQueue.empty() && get_direction() == 'L' && !get_is_blocked())
                    {
                        // First car to come to left bridge
                        // start timer thread
                        start_timer();
                    }

                    carRightQueue.push(passing_car.car);
                }
                else if (passing_car.to)
                {
                    if (carUpQueue.empty() && get_direction() == 'D' && !get_is_blocked())
                    {
                        start_timer();
                    }

                    carUpQueue.push(passing_car.car);
                }
                else
                {
                    if (carDownQueue.empty() && get_direction() == 'U' && !get_is_blocked())
                    {
                        start_timer();
                    }

                    carDownQueue.push(passing_car.car);
                }

            }
            else if (!carLeftQueue.empty() && get_direction() == 'L' && !get_is_blocked())
            {
                Car *car = carLeftQueue.front();
                carLeftQueue.pop();
                sem_post(&car->wait_passing);
                sleep_milli(PASS_DELAY);
            }
            else if (!carRightQueue.empty() && get_direction() == 'R' && !get_is_blocked())
            {
                Car *car = carRightQueue.front();
                carRightQueue.pop();
                sem_post(&car->wait_passing);
                sleep_milli(PASS_DELAY);
            }
            else if (!carUpQueue.empty() && get_direction() == 'U' && !get_is_blocked())
            {
                Car *car = carUpQueue.front();
                carUpQueue.pop();
                sem_post(&car->wait_passing);
                sleep_milli(PASS_DELAY);
            }
            else if (!carDownQueue.empty() && get_direction() == 'D' && !get_is_blocked())
            {
                Car *car = carDownQueue.front();
                carDownQueue.pop();
                sem_post(&car->wait_passing);
                sleep_milli(PASS_DELAY);
            }
            else if (get_is_blocked() && get_num_on_cross() == 0)
            {
                update_crossroad();
            }
            else if (get_should_join_timer())
            {
                join_timer();
            }
            else if (get_is_timer_started() && get_num_on_cross() == 0)
            {
                block_crossroad();
                update_crossroad();
            }
            else
            {
                sem_wait(&notifications);
            }
        }
    }

};

vector<NarrowBridge> narrowBridgeArray;
vector<Ferry *> ferry_array;
vector<Crossroad> crossroadArray;

vector<Car> car_array;

void *narrow_bridge_revers_timer(void *narrow_bridge_id)
{
    int i = *(int *)narrow_bridge_id;
    sleep_milli(narrowBridgeArray[i].maxWaitTime);
    if (narrowBridgeArray[i].get_is_timer_started())
    {
        narrowBridgeArray[i].block_bridge();
    }
    narrowBridgeArray[i].set_should_join_timer();
}

void *operate_bridge(void *threadid)
{
    
    int i = *((int *)threadid);
    NarrowBridge *bridge = &narrowBridgeArray[i];
    bridge->operate_bridge();
    return NULL;
}




void *crossroad_timer_func(void *crossroad_id)
{
    int i = *(int *)crossroad_id;
    sleep_milli(crossroadArray[i].maxWaitTime);
    if (crossroadArray[i].get_is_timer_started())
    {
        crossroadArray[i].block_crossroad();
    }
    crossroadArray[i].set_should_join_timer();
}

void *operate_crossroad(void *threadid)
{
    // Check the status of the bridge
    int i = *((int *)threadid);
    Crossroad *crossroad = &crossroadArray[i];
    crossroad->operate_crossroad();
    return NULL;
}


/*
Pass function for a car to pass narrow bridge
*/

//void pass(Car *car, char RCType, int RCID, int from, int to)

void *pass_rc(int car_id, char rc_type, int rc_id, int from, int to)
{
    if (rc_type == 'N')
    {
        NarrowBridge *bridge = &narrowBridgeArray[rc_id];
        Car *car = &car_array[car_id];
        bridge->enter_bridge(car, from, to);
        sem_wait(&car->wait_passing);
        WriteOutput(car_id, 'N', rc_id, START_PASSING);
        bridge->start_passing();
        sleep_milli(bridge->travelTime);
        bridge->finish_passing();
        WriteOutput(car_id, 'N', rc_id, FINISH_PASSING);
    }

    if (rc_type == 'F')
    {
        ferry_array[rc_id]->cross_ferry(car_id, to);
        WriteOutput(car_id, 'F', rc_id, START_PASSING);
        sleep_milli(ferry_array[rc_id]->travel_time);
        WriteOutput(car_id, 'F', rc_id, FINISH_PASSING);
    }

    if (rc_type == 'C')
    {
        struct Crossroad *crossroad = &crossroadArray[rc_id];
        WriteOutput(car_id, 'C', crossroad->ID, START_PASSING);
        sleep_milli(crossroad->travelTime);
        WriteOutput(car_id, 'C', crossroad->ID, FINISH_PASSING);
    }
}

/*
Operate car function is for threads to simulate car
*/
void *operate_car(void *p)
{
    Car *car = (Car *)p;
    for (size_t x = 0; x < car->pathLength; x++)
    {

        char curr_rc_type = car->RCType[x];
        int curr_rc_id = car->RCId[x];
        int curr_from = car->from[x];
        int curr_to = car->to[x];

        WriteOutput(car->id, curr_rc_type, curr_rc_id, TRAVEL);
        sleep_milli(car->travelTime);
        WriteOutput(car->id, curr_rc_type, curr_rc_id, ARRIVE);
        pass_rc(car->id, curr_rc_type, curr_rc_id, curr_from, curr_to);
    }

    return 0;
}

int main()
{

    InitWriteOutput();

    

    int narrowBridgesNum, crossroadsNum, carsNum, ferriesNum;

    scanf("%d", &narrowBridgesNum);
    for (int i = 0; i < narrowBridgesNum; i++)
    {
        int travelTime, maxWaitTime;
        scanf("%d %d", &travelTime, &maxWaitTime);
        NarrowBridge newNarrowBridge(i, travelTime, maxWaitTime);
        narrowBridgeArray.push_back(newNarrowBridge);
    }

    cin >> ferriesNum;
    for (int i = 0; i < ferriesNum; i++)
    {

        int travelTime, maxWaitTime, capacity;
        cin >> travelTime >> maxWaitTime >> capacity;
        ferry_array.push_back(new Ferry(i, capacity, travelTime, maxWaitTime));
    }

    scanf("%d", &crossroadsNum);
    for (int i = 0; i < crossroadsNum; i++)
    {
        int travelTime, maxWaitTime;
        scanf("%d %d", &travelTime, &maxWaitTime);
        Crossroad newCrossroad(i, travelTime, maxWaitTime);
        crossroadArray.push_back(newCrossroad);

    }

     cin >> carsNum;
    for (int i = 0; i < carsNum; i++)
    {
        int travelTime, pathLength;
        cin >> travelTime >> pathLength;
        Car newCar(i, travelTime, pathLength);
        for (int j = 0; j < newCar.pathLength; j++)
        {
            char rcType;
            int from, to, rcId;
            cin >> rcType >> rcId >> from >> to;
            newCar.RCType.push_back(rcType);
            newCar.RCId.push_back(rcId);
            newCar.from.push_back(from);
            newCar.to.push_back(to);
        }
        car_array.push_back(newCar);
    }

    vector<pthread_t> narrowbridgeThreads;
    for (int i = 0; i < narrowBridgesNum; i++)
    {
        pthread_t new_thread;
        pthread_create(&new_thread, NULL, operate_bridge, (void *)(&narrowBridgeArray[i].id));
        narrowbridgeThreads.push_back(new_thread);
    }



    vector<pthread_t> crossroadThreads;
    for (int i = 0; i < crossroadsNum; i++)
    {
        pthread_t new_thread;
        pthread_create(&new_thread, NULL, operate_crossroad, (void *)(&crossroadArray[i].ID));
        crossroadThreads.push_back(new_thread);
    }

    

    // Start car threads
 vector<pthread_t> carThreads;
    for (int i = 0; i < carsNum; i++)
    {
        pthread_t new_thread;
        pthread_create(&new_thread, NULL, operate_car, (void *)(&(car_array[i])));
        carThreads.push_back(new_thread);
    }

    for (int i = 0; i < carsNum; i++)
    {
        pthread_join(carThreads[i], NULL);
    }

    return 0;
}
