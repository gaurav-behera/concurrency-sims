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

struct coffeeType
{
    char name[100];
    int prepTime;
};

enum b_state
{
    AVAILABLE,
    OCCUPIED
};

enum c_state
{
    OUTSIDE,
    ORDERED,
    PREPARING,
    COMPLETED,
    LEFT,
    TOLTIME
};

struct customer
{
    int id;
    int arrivalTime;
    char order[100];
    int tolerenceTime;
    enum c_state state;
    int currTime;
    sem_t orderPlacedSignal;
    sem_t orderReceivedSignal;
    sem_t startPrepSignal;
    sem_t endPrepSignal;
    int waitTime;
};

struct barista
{
    int id;
    enum b_state state;
    int currTime;
};

int baristaCount, coffeeTypeCount, customerCount;
struct customer *customers;
struct coffeeType *coffeeTypes;
struct barista *baristas;

sem_t baristas_sem; // available baristaa
pthread_mutex_t baristaLock;
pthread_mutex_t customerLock;
pthread_mutex_t orderLock;

int coffeeWasted;

void getInput()
{
    scanf("%d %d %d", &baristaCount, &coffeeTypeCount, &customerCount);

    coffeeTypes = malloc(sizeof(struct coffeeType) * coffeeTypeCount);
    customers = malloc(sizeof(struct customer) * customerCount);
    baristas = malloc(sizeof(struct barista) * baristaCount);

    for (int i = 0; i < coffeeTypeCount; i++)
    {
        scanf("%s %d", coffeeTypes[i].name, &coffeeTypes[i].prepTime);
    }
    for (int i = 0; i < customerCount; i++)
    {
        scanf("%d %s %d %d", &customers[i].id, customers[i].order, &customers[i].arrivalTime, &customers[i].tolerenceTime);
    }
}

void initializeValues()
{
    int rc;
    for (int i = 0; i < baristaCount; i++)
    {
        baristas[i].id = i + 1;
        baristas[i].currTime = 0;
        baristas[i].state = AVAILABLE;
    }
    rc = sem_init(&baristas_sem, 0, baristaCount);
    assert(rc == 0);

    for (int i = 0; i < customerCount; i++)
    {
        customers[i].currTime = 0;
        customers[i].state = OUTSIDE;
        rc = sem_init(&customers[i].orderPlacedSignal, 0, 0);
        assert(rc == 0);
        rc = sem_init(&customers[i].orderReceivedSignal, 0, 0);
        assert(rc == 0);
        rc = sem_init(&customers[i].startPrepSignal, 0, 0);
        assert(rc == 0);
        rc = sem_init(&customers[i].endPrepSignal, 0, 0);
        assert(rc == 0);
    }

    rc = pthread_mutex_init(&baristaLock, NULL);
    assert(rc == 0);
    rc = pthread_mutex_init(&customerLock, NULL);
    assert(rc == 0);
    rc = pthread_mutex_init(&orderLock, NULL);
    assert(rc == 0);
    coffeeWasted = 0;
}

int getPreparationTime(char *coffeeType)
{
    for (int i = 0; i < coffeeTypeCount; i++)
    {
        if (strcmp(coffeeTypes[i].name, coffeeType) == 0)
        {
            return coffeeTypes[i].prepTime;
        }
    }
    return -1;
}

void prepareCoffee(char *order)
{
    int prepTime = getPreparationTime(order);
    sleep(prepTime);
    return;
}

struct barista *getBarista()
{
    sem_wait(&baristas_sem); // if all baristas are occupied

    struct barista *currBarista = NULL;
    pthread_mutex_lock(&baristaLock);
    for (int i = 0; i < baristaCount; i++)
    {
        if (baristas[i].state == AVAILABLE)
        {
            currBarista = &baristas[i];
            break;
        }
    }
    currBarista->state = OCCUPIED;
    pthread_mutex_unlock(&baristaLock);
    return currBarista;
}

struct customer *getCustomer()
{
    struct customer *currCustomer = NULL;
    pthread_mutex_lock(&customerLock);
    for (int i = 0; i < customerCount; i++)
    {
        if (customers[i].state == OUTSIDE || customers[i].state == ORDERED)
        {
            if (!currCustomer || customers[i].arrivalTime < currCustomer->arrivalTime)
            {
                currCustomer = &customers[i];
            }
            else if (customers[i].arrivalTime < currCustomer->arrivalTime && customers[i].id < currCustomer->id)
            {
                currCustomer = &customers[i];
            }
        }
    }
    pthread_mutex_unlock(&customerLock);
    return currCustomer;
}

bool checkAllCustomers()
{
    int retVal = true;
    for (int i = 0; i < customerCount; i++)
    {
        if (customers[i].state != LEFT)
        {
            retVal = false;
            break;
        }
    }
    return retVal;
}

bool checkAllBaristas()
{
    int retVal = true;
    for (int i = 0; i < baristaCount; i++)
    {
        if (baristas[i].state != AVAILABLE)
        {
            retVal = false;
            break;
        }
    }
    return retVal;
}

void *simulateOrder(void *args)
{
    // while (checkAllCustomers && checkAllBaristas)
    // {
    pthread_mutex_lock(&orderLock);
    struct customer *currCustomer = getCustomer();
    sem_wait(&currCustomer->orderPlacedSignal);

    clock_t startTime = clock();

    struct barista *currBarista = getBarista();
    pthread_mutex_lock(&baristaLock);
    currBarista->state = OCCUPIED;
    pthread_mutex_unlock(&baristaLock);

    int interval = (clock() - startTime) / CLOCKS_PER_SEC;
    currCustomer->currTime += interval;
    if (currBarista->currTime > currCustomer->currTime)
    {
        currCustomer->currTime = currBarista->currTime;
    }
    else
    {
        currBarista->currTime = currCustomer->currTime;
    }

    sem_post(&currCustomer->orderReceivedSignal);

    sem_wait(&currCustomer->startPrepSignal);

    pthread_mutex_unlock(&orderLock);

    if (currCustomer->state == PREPARING)
    {
        sleep(1);
        currBarista->currTime++;
        currCustomer->currTime++;
        printf(CYAN_COLOR "Barista %d begins preparing the order of customer %d at %d second(s)\n", currBarista->id, currCustomer->id, currCustomer->currTime);
        printf(RESET_COLOR);

        prepareCoffee(currCustomer->order);

        pthread_mutex_lock(&customerLock);
        currCustomer->state = COMPLETED;
        pthread_mutex_unlock(&customerLock);
        sem_post(&currCustomer->endPrepSignal);

        currBarista->currTime += getPreparationTime(currCustomer->order);
        currCustomer->currTime += getPreparationTime(currCustomer->order);
        printf(BLUE_COLOR "Barista %d completes the order of customer %d at %d second(s)\n", currBarista->id, currCustomer->id, currCustomer->currTime);
        printf(RESET_COLOR);

        pthread_mutex_lock(&customerLock);
        currCustomer->state = LEFT;
        pthread_mutex_unlock(&customerLock);

        pthread_mutex_lock(&baristaLock);
        currBarista->state = AVAILABLE;
        pthread_mutex_unlock(&baristaLock);
        sem_post(&baristas_sem);
    }
    else
    {
        pthread_mutex_lock(&baristaLock);
        currBarista->state = AVAILABLE;
        pthread_mutex_unlock(&baristaLock);
        sem_post(&baristas_sem);
    }
    // }
    return NULL;
}

void *simulateCustomer(void *args)
{
    struct customer *cust = (struct customer *)(args);
    int customerId = cust->id;

    cust->currTime = 0;
    int arrivalTime = cust->arrivalTime;
    int exitTime = arrivalTime + cust->tolerenceTime + 1;
    sleep(arrivalTime);
    cust->currTime += arrivalTime;
    
    printf(WHITE_COLOR "Customer %d arrives at %d second(s)\n", customerId, cust->currTime);
    printf(YELLOW_COLOR "Customer %d orders an %s\n", customerId, cust->order);
    printf(RESET_COLOR);

    pthread_mutex_lock(&customerLock);
    cust->state = ORDERED;
    pthread_mutex_unlock(&customerLock);
    sem_post(&cust->orderPlacedSignal);

    bool end1 = false;
    // wait till tolerenceTime if coffee not prepared
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1)
    {
        perror("clock_gettime");
    }
    ts.tv_sec += cust->tolerenceTime + 2;
    // ts.tv_nsec += 1000000;
    int rc;
    while ((rc = sem_timedwait(&cust->orderReceivedSignal, &ts)) == -1 && errno == EINTR)
    {
        continue;
    }
    if (rc == -1)
    {
        if (errno == ETIMEDOUT)
        {
            // timeout
            if (cust->state != COMPLETED || cust->state != LEFT)
            {
                // pthread_mutex_lock(&customerLock);
                // cust->waitTime = exitTime - cust->arrivalTime;
                // pthread_mutex_unlock(&customerLock);
                // printf(RED_COLOR "Customer %d leaves without their order at %d second(s)\n", customerId, exitTime);
                // printf(RESET_COLOR);
                pthread_mutex_lock(&customerLock);
                cust->state = TOLTIME;
                // cust->state = LEFT;
                pthread_mutex_unlock(&customerLock);

                // to end thread
                sem_post(&cust->startPrepSignal);
                end1 = true;
            }
        }
        else
        {
            perror("sem_timedwait");
            return NULL;
        }
        // return NULL;
    }

    if (!end1)
    {

        // barista assigned before timeout
        pthread_mutex_lock(&customerLock);
        cust->state = PREPARING;
        pthread_mutex_unlock(&customerLock);

        sem_post(&cust->startPrepSignal);

        while ((rc = sem_timedwait(&cust->endPrepSignal, &ts)) == -1 && errno == EINTR)
        {
            continue;
        }
        if (rc == -1)
        {
            if (errno == ETIMEDOUT)
            {
                // timeout
                if (cust->state != COMPLETED || cust->state != LEFT)
                {
                    // pthread_mutex_lock(&customerLock);
                    // cust->waitTime = exitTime - cust->arrivalTime;
                    // pthread_mutex_unlock(&customerLock);
                    // printf(RED_COLOR "Customer %d leaves without their order at %d second(s)\n", customerId, exitTime);
                    // printf(RESET_COLOR);
                    pthread_mutex_lock(&customerLock);
                    cust->state = TOLTIME;
                    pthread_mutex_unlock(&customerLock);
                    coffeeWasted++;
                }
            }
            else
            {
                perror("sem_timedwait");
                return NULL;
            }
            // return NULL;
        }
    }

    if (cust->state == TOLTIME)
    {
        cust->waitTime = exitTime - cust->arrivalTime;
        printf(RED_COLOR "Customer %d leaves without their order at %d second(s)\n", customerId, exitTime);
        printf(RESET_COLOR);
    }
    else
    {
        printf(GREEN_COLOR "Customer %d leaves with their order at %d second(s)\n", customerId, cust->currTime);
        printf(RESET_COLOR);

        cust->waitTime = cust->currTime - cust->arrivalTime - getPreparationTime(cust->order);
    }

    return NULL;
}

void freeValues()
{
    free(coffeeTypes);
    free(customers);
    free(baristas);
}

int main()
{
    getInput();
    initializeValues();

    pthread_t customerThreads[customerCount], orderThreads[customerCount];

    for (int i = 0; i < customerCount; i++)
    {
        pthread_create(&customerThreads[i], NULL, simulateCustomer, &customers[i]);
    }
    for (int i = 0; i < customerCount; i++)
    {
        pthread_create(&orderThreads[i], NULL, simulateOrder, NULL);
    }
    for (int i = 0; i < customerCount; i++)
    {
        pthread_join(customerThreads[i], NULL);
        pthread_join(orderThreads[i], NULL);
    }

    // calculating average wait time
    int totalWaitTime = 0;
    for (int i = 0; i < customerCount; i++)
    {
        // printf("%d\n", customers[i].waitTime);
        totalWaitTime += customers[i].waitTime;
    }
    double avgWaitTime = ((double)(totalWaitTime)) / customerCount;

    printf("\nAverage wait time: %2f sec", avgWaitTime);
    printf("\n%d coffee wasted\n", coffeeWasted);
    freeValues();
    return 0;
}