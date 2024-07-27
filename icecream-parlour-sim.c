#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "colors.h"
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <errno.h>

#define MAXORDERS 10000
#define MAXCUST 10000

struct flavourType
{
    char name[100];
    int prepTime;
};

struct toppingType
{
    char name[100];
    int prepTime;
    int quantity;
    int idx;
};

enum m_state
{
    BLOCKED,
    AVAILABLE,
    OCCUPIED
};

enum o_state
{
    OUTSIDE,
    ORDERED,
    PREPARING,
    COMPLETED,
    NO_MACHINES,
    NO_TOPPINGS
};

struct customer
{
    int customerID;
    int arrivalTime;
    int numberOfOrders;
    int idx;
    bool ordered;
};

struct order
{
    struct customer *customer;
    int orderId;
    struct flavourType *flavour;
    struct toppingType **toppings;
    int numOfToppings;
    enum o_state state;
    int currTime;
    sem_t orderPlacedSignal;
    sem_t orderReceivedSignal;
    sem_t startPrepSignal;
    sem_t endPrepSignal;
};

struct machine
{
    int id;
    enum m_state state;
    int currTime;
    int startTime;
    int endTime;
};

int machineCount, flavourTypeCount, toppingTypeCount, parlourCapacity;
struct customer *customers;
struct machine *machines;
struct flavourType *flavours;
struct toppingType *toppings;
struct order *orders;

int totalOrders, totalCustomers;
int currentCustomerCount, parlourClosingTime;
bool parClosed, parCloseTime;

pthread_mutex_t toppingsLock;
sem_t machines_sem; // available baristaa
pthread_mutex_t machineLock;
pthread_mutex_t ordersLock;
pthread_mutex_t customerLock;

struct flavourType *getFlavourType(char *name)
{
    for (int i = 0; i < flavourTypeCount; i++)
    {
        if (strcmp(flavours[i].name, name) == 0)
        {
            return &flavours[i];
        }
    }
    return NULL;
}

struct toppingType *getToppingType(char *name)
{
    for (int i = 0; i < toppingTypeCount; i++)
    {
        if (strcmp(toppings[i].name, name) == 0)
        {
            return &toppings[i];
        }
    }
    return NULL;
}

void getInputAndInitialize()
{
    scanf("%d %d %d %d", &machineCount, &parlourCapacity, &flavourTypeCount, &toppingTypeCount);

    customers = malloc(sizeof(struct customer) * MAXCUST);
    machines = malloc(sizeof(struct machine) * machineCount);
    flavours = malloc(sizeof(struct flavourType) * flavourTypeCount);
    toppings = malloc(sizeof(struct toppingType) * toppingTypeCount);
    orders = malloc(sizeof(struct order) * MAXORDERS);

    parlourClosingTime = 0;
    parClosed = false;
    for (int i = 0; i < machineCount; i++)
    {
        scanf("%d %d", &machines[i].startTime, &machines[i].endTime);
        machines[i].currTime = 0;
        machines[i].id = i + 1;
        machines[i].state = BLOCKED;
        parlourClosingTime = parlourClosingTime > machines[i].endTime ? parlourClosingTime : machines[i].endTime;
    }

    for (int i = 0; i < flavourTypeCount; i++)
    {
        scanf("%s %d", flavours[i].name, &flavours[i].prepTime);
    }
    for (int i = 0; i < toppingTypeCount; i++)
    {
        scanf("%s %d", toppings[i].name, &toppings[i].quantity);
        toppings[i].prepTime = 0;
        toppings[i].idx = i;
    }

    totalOrders = 0;
    totalCustomers = 0;
    currentCustomerCount = 0;

    char c;
    scanf("%c", &c);
    for (int i = 0; i < MAXCUST; i++)
    {
        char input[4096];
        fgets(input, sizeof(input), stdin);
        if (strlen(input) == 1)
        {
            break;
        }
        char *token = strtok(input, " \n");
        customers[i].customerID = atoi(token);
        token = strtok(NULL, " \n");
        customers[i].arrivalTime = atoi(token);
        token = strtok(NULL, " \n");
        customers[i].numberOfOrders = atoi(token);
        customers[i].idx = i;
        customers[i].ordered = false;
        // scanf("%d %d\n", &customers[i].arrivalTime, &customers[i].numberOfOrders);
        for (int j = 0; j < customers[i].numberOfOrders; j++)
        {
            char input[4096];
            fgets(input, sizeof(input), stdin);
            // printf(".%s.", input);
            orders[totalOrders].numOfToppings = 0;
            for (int k = 0; k < strlen(input); k++)
            {
                if (input[k] == ' ')
                {
                    orders[totalOrders].numOfToppings++;
                }
            }

            orders[totalOrders].flavour = getFlavourType(input);
            orders[totalOrders].toppings = malloc(sizeof(struct toppingType *) * orders[totalOrders].numOfToppings);

            char *token = strtok(input, " \n");
            orders[totalOrders].flavour = getFlavourType(token);

            for (int k = 0; k < orders[totalOrders].numOfToppings; k++)
            {
                token = strtok(NULL, " \n");
                orders[totalOrders].toppings[k] = getToppingType(token);
            }

            orders[totalOrders].currTime = 0;
            orders[totalOrders].customer = &customers[i];
            orders[totalOrders].orderId = j + 1;
            orders[totalOrders].state = OUTSIDE;

            int rc;
            rc = sem_init(&orders[totalOrders].orderPlacedSignal, 0, 0);
            assert(rc == 0);
            rc = sem_init(&orders[totalOrders].orderReceivedSignal, 0, 0);
            assert(rc == 0);
            rc = sem_init(&orders[totalOrders].startPrepSignal, 0, 0);
            assert(rc == 0);
            rc = sem_init(&orders[totalOrders].endPrepSignal, 0, 0);
            assert(rc == 0);

            totalOrders++;
        }
        totalCustomers++;
    }

    int rc;
    rc = pthread_mutex_init(&toppingsLock, NULL);
    assert(rc == 0);
    rc = pthread_mutex_init(&machineLock, NULL);
    assert(rc == 0);
    rc = pthread_mutex_init(&ordersLock, NULL);
    assert(rc == 0);
    rc = pthread_mutex_init(&customerLock, NULL);
    assert(rc == 0);
    rc = sem_init(&machines_sem, 0, 0);
    assert(rc == 0);
}

int getPreparationTime(struct order *order)
{
    int prepTime = 0;
    prepTime += order->flavour->prepTime;
    for (int i = 0; i < order->numOfToppings; i++)
    {
        prepTime += order->toppings[i]->prepTime;
    }
    return prepTime;
}

void prepareOrder(struct order *order)
{
    pthread_mutex_lock(&toppingsLock);
    for (int i = 0; i < order->numOfToppings; i++)
    {
        (order->toppings[i]->quantity)--;
    }
    pthread_mutex_unlock(&toppingsLock);
    sleep(getPreparationTime(order));
    return;
}

struct machine *getMachineToUse()
{
    sem_wait(&machines_sem); // if no machines are available
    struct machine *currMachine = NULL;
    pthread_mutex_lock(&machineLock);
    for (int i = 0; i < machineCount; i++)
    {
        if (machines[i].state == AVAILABLE)
        {
            if (!currMachine)
            {
                currMachine = &machines[i];
            }
            else if (machines[i].id < currMachine->id)
            {
                currMachine = &machines[i];
            }
        }
    }
    currMachine->state = OCCUPIED;
    pthread_mutex_unlock(&machineLock);
    return currMachine;
}

struct machine *getIdealMachineToUse(struct order *order)
{
    struct machine *currMachine = NULL;
    int prepTime = getPreparationTime(order);
    pthread_mutex_lock(&machineLock);
    for (int i = 0; i < machineCount; i++)
    {
        if (machines[i].state == AVAILABLE && (machines[i].currTime + prepTime <= machines[i].endTime))
        {
            currMachine = &machines[i];
            break;
        }
    }
    pthread_mutex_unlock(&machineLock);
    return currMachine;
}

struct order *getOrderToPrepare(int closingTime, int currTime)
{
    struct order *currOrder = NULL;
    pthread_mutex_lock(&ordersLock);
    for (int i = 0; i < totalOrders; i++)
    {
        currTime = currTime > orders[i].currTime ? currTime : orders[i].currTime;
        int remainingTime = closingTime - currTime;
        if ((orders[i].state == ORDERED) && getPreparationTime(&orders[i]) <= remainingTime)
        {
            // first basis is customers arrival time
            if (!currOrder)
            {
                currOrder = &orders[i];
            }
            else if (orders[i].customer->arrivalTime < currOrder->customer->arrivalTime)
            {
                currOrder = &orders[i];
            }
            else if (orders[i].customer->arrivalTime == currOrder->customer->arrivalTime)
            {
                // next basis is customer id if same arrival time
                if (orders[i].customer->customerID < currOrder->customer->customerID)
                {
                    currOrder = &orders[i];
                }
                else if (orders[i].customer->customerID == currOrder->customer->customerID)
                {
                    // next basis is order id if same customer
                    if (orders[i].orderId < currOrder->orderId)
                    {
                        currOrder = &orders[i];
                    }
                }
                else
                {
                }
            }
            else
            {
            }
        }
    }
    // currOrder->state = READY_TO_PREPARE;
    pthread_mutex_unlock(&ordersLock);
    return currOrder;
}

bool checkCustomerOrdersFullfillment(struct customer *cust)
{
    bool res = true;
    int refCounts[toppingTypeCount];
    bool close = false;
    for (int i = 0; i < toppingTypeCount; i++)
    {
        refCounts[i] = toppings[i].quantity;
        if (toppings[i].quantity == 0)
        {
            close = true;
        }
    }
    int currTime = 0;
    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust && orders[i].state < PREPARING)
        {
            currTime = currTime > orders[i].currTime ? currTime : orders[i].currTime;
        }
    }

    if (close)
    {
        parClosed = true;
        parlourClosingTime = currTime;
    }

    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust && orders[i].state < PREPARING)
        {
            for (int j = 0; j < orders[i].numOfToppings; j++)
            {
                refCounts[orders[i].toppings[j]->idx]--;
                if (refCounts[orders[i].toppings[j]->idx] == -1)
                {
                    res = false;
                }
                if (refCounts[orders[i].toppings[j]->idx] == -2)
                {
                    refCounts[orders[i].toppings[j]->idx] = -1;
                }
            }
        }
    }
    return res;
}

void *simulateCustomerOrder(void *args)
{
    struct order *order = (struct order *)(args);
    // int orderId = order->orderId;
    // int customerId = order->customer->customerID;

    // pthread_mutex_lock(&ordersLock);
    // order->state = ORDERED;
    // pthread_mutex_unlock(&ordersLock);

    sem_post(&order->orderPlacedSignal);

    // wait till parlour closes
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    {
        perror("clock_gettime");
    }
    if (order->state == NO_TOPPINGS)
    {
        return (void *)&order->currTime;
    }
    ts.tv_sec += parlourClosingTime - order->currTime + 1;
    int rc;
    while ((rc = sem_timedwait(&order->orderReceivedSignal, &ts)) == -1 && errno == EINTR)
    {
        continue;
    }
    if (rc == -1)
    {
        if (errno == ETIMEDOUT)
        {
            // timeout
            // printf(RED_COLOR "Customer %d leaves without their order at %d second(s)\n", order->customer->customerID, parlourClosingTime);
            printf(RESET_COLOR);
            pthread_mutex_lock(&ordersLock);
            order->state = NO_MACHINES;
            pthread_mutex_unlock(&ordersLock);

            // to end thread
            // sem_post(&cust->startPrepSignal);
        }
        else
        {
            perror("sem_timedwait");
        }
        return (void *)&parlourClosingTime;
    }
    // sem_wait(&order->orderReceivedSignal);
    if (order->state == NO_TOPPINGS)
    {
        // stop all other orders of the customer
        sem_post(&order->startPrepSignal);
        // sem_wait(&order->endPrepSignal);

        pthread_mutex_lock(&ordersLock);
        for (int i = 0; i < totalOrders; i++)
        {
            if (orders[i].customer == order->customer && orders[i].state < PREPARING)
            {
                orders[i].state = NO_TOPPINGS;
            }
        }
        pthread_mutex_unlock(&ordersLock);
        return (void *)&order->currTime;
    }

    pthread_mutex_lock(&ordersLock);
    order->state = PREPARING;
    pthread_mutex_unlock(&ordersLock);

    sem_post(&order->startPrepSignal);

    sem_wait(&order->endPrepSignal);

    pthread_mutex_lock(&ordersLock);
    order->state = COMPLETED;
    pthread_mutex_unlock(&ordersLock);

    return (void *)&order->currTime;
}

void *simulateCustomer(void *args)
{
    struct customer *cust = (struct customer *)(args);
    int customerId = cust->customerID;
    int currTime = 0;
    sleep(cust->arrivalTime);
    while (cust->idx != 0 && !customers[cust->idx - 1].ordered)
    {
    }
    currTime += cust->arrivalTime;

    pthread_mutex_lock(&ordersLock);
    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust)
        {
            orders[i].currTime = currTime;
        }
    }
    pthread_mutex_unlock(&ordersLock);
    cust->ordered = true;


    printf(WHITE_COLOR "Customer %d enters at %d second(s)\n", customerId, currTime);

    pthread_mutex_lock(&customerLock);
    if (currentCustomerCount == parlourCapacity)
    {
        pthread_mutex_lock(&ordersLock);
        for (int i = 0; i < totalOrders; i++)
        {
            if (orders[i].customer == cust)
            {
                orders[i].state = COMPLETED;
            }
        }
        pthread_mutex_unlock(&ordersLock);

        printf(RED_COLOR "Customer %d left as parlour is full\n", customerId);
        pthread_mutex_unlock(&customerLock);

        return NULL;
    }
    pthread_mutex_unlock(&customerLock);

    printf(YELLOW_COLOR "Customer %d orders %d ice cream(s)\n", customerId, cust->numberOfOrders);

    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust)
        {
            printf("Ice cream %d: ", orders[i].orderId);
            printf("%s", orders[i].flavour->name);
            for (int j = 0; j < orders[i].numOfToppings; j++)
            {
                printf(" %s", orders[i].toppings[j]->name);
            }
            printf("\n");
        }
    }
    printf(RESET_COLOR);

    if (checkCustomerOrdersFullfillment(cust) == false)
    {
        pthread_mutex_lock(&ordersLock);
        for (int i = 0; i < totalOrders; i++)
        {
            if (orders[i].customer == cust)
            {
                orders[i].state = NO_TOPPINGS;
            }
        }
        pthread_mutex_unlock(&ordersLock);
        printf(RED_COLOR "Customer %d left at %d second(s) with an unfulfilled order\n", cust->customerID, cust->arrivalTime);
        printf(RESET_COLOR);
        return NULL;
    }

    pthread_mutex_lock(&customerLock);
    currentCustomerCount++;
    pthread_mutex_unlock(&customerLock);

    // orders can be preapred from 1 sec later
    sleep(1);
    pthread_mutex_lock(&ordersLock);
    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust)
        {
            orders[i].currTime++;
        }
    }
    pthread_mutex_unlock(&ordersLock);

    if (checkCustomerOrdersFullfillment(cust) == false)
    {
        pthread_mutex_lock(&ordersLock);
        for (int i = 0; i < totalOrders; i++)
        {
            if (orders[i].customer == cust)
            {
                orders[i].state = NO_TOPPINGS;
            }
        }
        pthread_mutex_unlock(&ordersLock);
        printf(RED_COLOR "Customer %d left at %d second(s) with an unfulfilled order\n", cust->customerID, cust->arrivalTime + 1);
        printf(RESET_COLOR);
        return NULL;
    }

    pthread_t custOrderThreads[cust->numberOfOrders];
    int j = 0;
    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust)
        {
            // current customer's order
            pthread_mutex_lock(&ordersLock);
            orders[i].state = ORDERED;
            pthread_mutex_unlock(&ordersLock);
            pthread_create(&custOrderThreads[j], NULL, simulateCustomerOrder, &orders[i]);
            j++;
        }
    }
    int *orderCompletionTimes[cust->numberOfOrders];
    int finalTime = 0;
    for (id_t i = 0; i < cust->numberOfOrders; i++)
    {
        pthread_join(custOrderThreads[i], (void **)&orderCompletionTimes[i]);
        if (*orderCompletionTimes[i] > finalTime)
        {
            finalTime = *orderCompletionTimes[i];
        }
    }

    // check status of orders
    bool allCompleted = true, machineRejection = false, toppingsRejection = false;
    for (int i = 0; i < totalOrders; i++)
    {
        if (orders[i].customer == cust)
        {
            if (orders[i].state != COMPLETED)
            {
                allCompleted = false;
            }
            if (orders[i].state == NO_MACHINES)
            {
                machineRejection = true;
            }
            if (orders[i].state == NO_TOPPINGS)
            {
                toppingsRejection = true;
            }
        }
    }
    // printf("customer %d over:AC:%d, Mach:%d, Topping:%d\n", customerId, allCompleted, machineRejection, toppingsRejection);
    if (allCompleted)
    {
        printf(GREEN_COLOR "Customer %d has collected their order(s) and left at %d second(s)\n", customerId, finalTime);
    }
    else if (machineRejection)
    {
        printf(RED_COLOR "Customer %d was not serviced due to unavailability of machines\n", customerId);
    }
    else if (parClosed || toppingsRejection)
    {
        printf(RED_COLOR "Customer %d left at %d second(s) with an unfulfilled order\n", customerId, finalTime);
    }
    printf(RESET_COLOR);

    pthread_mutex_lock(&customerLock);
    currentCustomerCount--;
    pthread_mutex_unlock(&customerLock);

    return NULL;
}

void *simulateMachine(void *args)
{
    struct machine *currMachine = (struct machine *)(args);
    int machineID = currMachine->id;
    sleep(currMachine->startTime);

    pthread_mutex_lock(&machineLock);
    currMachine->state = AVAILABLE;
    currMachine->currTime = currMachine->startTime;
    pthread_mutex_unlock(&machineLock);

    // make itself available
    sem_post(&machines_sem);

    printf(ORANGE_COLOR "Machine %d has started working at %d second(s)\n", machineID, currMachine->startTime);
    printf(RESET_COLOR);

    // prepare orders
    while (1)
    {
        struct order *currOrder = getOrderToPrepare(currMachine->endTime, currMachine->currTime);
        
        if (parClosed)
        {
            break;
        }

        if (currOrder != NULL && currOrder->state == NO_TOPPINGS)
        {
            sem_wait(&currOrder->orderPlacedSignal);
            sem_post(&currOrder->orderReceivedSignal);
            continue;
        }

        // wait till order is placed
        if (currOrder == NULL || getIdealMachineToUse(currOrder) != currMachine)
        {
            currOrder = NULL;
            int currTime = currMachine->currTime;
            int startTime = clock();
            while (currOrder == NULL)
            {
                // printf("%d %d\n", currMachine->id, currMachine->currTime);
                // int startTime = clock();
                currOrder = getOrderToPrepare(currMachine->endTime, currMachine->currTime);
                if (parClosed)
                {
                    // currOrder->currTime = currMachine->currTime;
                    break;
                }
                if (currOrder != NULL)
                {
                    // printf("%d prep %d.\n", currMachine->id, currOrder->orderId);
                    if (currOrder->state == NO_TOPPINGS)
                    {
                        sem_wait(&currOrder->orderPlacedSignal);
                        sem_post(&currOrder->orderReceivedSignal);
                        break;
                    }
                    if (getIdealMachineToUse(currOrder) == currMachine)
                    {
                        break;
                    }
                    else
                    {
                        currOrder = NULL;
                    }
                }

                int interval = (clock() - startTime) / CLOCKS_PER_SEC;
                currMachine->currTime = currTime + interval;
                if (currMachine->currTime >= currMachine->endTime)
                {
                    // return NULL;
                    break;
                }
            }
        }
        if (currOrder == NULL)
        {
            // only possible due to break statement when machine times out
            break;
        }
        if (parClosed)
        {
            currOrder->currTime = currMachine->currTime;
            break;
        }
        if (currOrder != NULL && currOrder->state == NO_TOPPINGS)
        {
            // sem_wait(&currOrder->orderPlacedSignal);
            // sem_post(&currOrder->orderReceivedSignal);
            continue;
        }
        // printf("%d prep %d\n", currMachine->id, currOrder->orderId);

        sem_wait(&currOrder->orderPlacedSignal);
        if (currMachine->currTime > currOrder->currTime)
        {
            currOrder->currTime = currMachine->currTime;
        }
        else
        {
            currMachine->currTime = currOrder->currTime;
        }
        if (parClosed)
        {
            break;
        }
        if (getIdealMachineToUse(currOrder) == NULL)
        {
            currOrder->state = NO_MACHINES;
            continue;
        }
        if (checkCustomerOrdersFullfillment(currOrder->customer) == false)
        {
            currOrder->state = NO_TOPPINGS;
            sem_post(&currOrder->orderReceivedSignal);
            continue;
        }

        sem_post(&currOrder->orderReceivedSignal);
        sem_wait(&currOrder->startPrepSignal);

        if (currOrder->state == PREPARING)
        {
            pthread_mutex_lock(&machineLock);
            currMachine->state = OCCUPIED;
            pthread_mutex_unlock(&machineLock);
            printf(CYAN_COLOR "Machine %d starts preparing ice cream %d of customer %d at %d seconds(s)\n", currMachine->id, currOrder->orderId, currOrder->customer->customerID, currOrder->currTime);
            printf(RESET_COLOR);

            prepareOrder(currOrder);
            currMachine->currTime += getPreparationTime(currOrder);
            currOrder->currTime += getPreparationTime(currOrder);

            // if (parClosed)
            // {
            //     break;
            // }

            printf(LIGHT_BLUE_COLOR "Machine %d completes preparing ice cream %d of customer %d at %d seconds(s)\n", currMachine->id, currOrder->orderId, currOrder->customer->customerID, currOrder->currTime);
            printf(RESET_COLOR);
            sem_post(&currOrder->endPrepSignal);
            sleep(1);
            currMachine->currTime++;
        }
        else
        {
            sem_post(&currOrder->endPrepSignal);
        }

        if (currMachine->currTime >= currMachine->endTime)
        {
            break;
        }
        pthread_mutex_lock(&machineLock);
        currMachine->state = AVAILABLE;
        pthread_mutex_unlock(&machineLock);
        sem_post(&machines_sem);
    }

    pthread_mutex_lock(&machineLock);
    currMachine->state = BLOCKED;

    if (!parClosed)
    {
    currMachine->currTime = currMachine->endTime;
    }
    

    pthread_mutex_unlock(&machineLock);

    // if (parClosed)
    // {
    //     pthread_mutex_lock(&machineLock);
    //     currMachine->currTime = parlourClosingTime;
    //     pthread_mutex_unlock(&machineLock);
    // }

    printf(ORANGE_COLOR "Machine %d has stopped working at %d second(s)", machineID, currMachine->currTime);
    if (parClosed)
    {
        printf(" as limited toppings are over.");
    }
    printf("\n");
    
    printf(RESET_COLOR);

    return NULL;
}

void freeValues()
{
    free(customers);
    free(machines);
    free(flavours);
    free(toppings);
    for (int i = 0; i < totalOrders; i++)
    {
        free(orders[i].toppings);
    }
    free(orders);
}

int main()
{
    getInputAndInitialize();
    // initializeValues();
    // printf("total m: %d, c: %d\n", machineCount, totalCustomers);

    pthread_t customerThreads[totalCustomers], machineThreads[machineCount];

    for (int i = 0; i < machineCount; i++)
    {
        pthread_create(&machineThreads[i], NULL, simulateMachine, &machines[i]);
    }
    for (int i = 0; i < totalCustomers; i++)
    {
        pthread_create(&customerThreads[i], NULL, simulateCustomer, &customers[i]);
    }

    for (int i = 0; i < machineCount; i++)
    {
        pthread_join(machineThreads[i], NULL);
    }

    for (int i = 0; i < totalCustomers; i++)
    {
        pthread_join(customerThreads[i], NULL);
    }
    printf("Parlour Closed");
    if (parClosed)
    {
        printf(" as limited ingredients are over.\n");
    }
    else
    {
        printf(" as all machines have stopped working.\n");
    }

    // freeValues();
    return 0;
}