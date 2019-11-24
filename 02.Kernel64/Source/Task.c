#include "Task.h"
#include "Descriptor.h"
#define A 16807L
#define M 2147483647L

static SCHEDULER gs_stScheduler;
static TCBPOOLMANAGER gs_stTCBPoolManager;
static int strm = 1;
static int sum = 0;
static int findT;
static unsigned int gs_qwRandomValue;

static long ln[16] = {
    0L, 1973272912L, 747177549L, 20464843L, 640830765L, 1098742207L,
    78126602L, 84743774L, 831312807L, 124667236L, 1172177002L,
    1124933064L, 1223960546L, 1878892440L, 1449793615L, 553303732L
};

long ranf(){
    short *p, *q, k;
    long hi, lo;
    p = (short*)&ln[strm];
    hi = *(p+1) * A;
    *(p+1) = 0;
    lo = ln[strm] * A;
    p = (short*)&lo;
    hi += *(p+1);
    q = (short*)&hi;
    *(p+1) = *q & 0x7FFF;
    k = *(q+1) << 1;
    if(*q & 0x7FFF){
        k++;
    }
    lo -= M;
    lo += k;
    if(lo < 0){
        lo += M;
    }
    ln[strm] = lo;
    return (long)lo * 4 - 10;
}

static void kInitializeTCBPool( void )
{
    int i;
    
    kMemSet( &( gs_stTCBPoolManager ), 0, sizeof( gs_stTCBPoolManager ) );
    
    gs_stTCBPoolManager.pstStartAddress = ( TCB* ) TASK_TCBPOOLADDRESS;
    kMemSet( TASK_TCBPOOLADDRESS, 0, sizeof( TCB ) * TASK_MAXCOUNT );

    for( i = 0 ; i < TASK_MAXCOUNT ; i++ )
    {
        gs_stTCBPoolManager.pstStartAddress[ i ].stLink.qwID = i;
    }
    
    gs_stTCBPoolManager.iMaxCount = TASK_MAXCOUNT;
    gs_stTCBPoolManager.iAllocatedCount = 1;
}

static TCB* kAllocateTCB( void )
{
    TCB* pstEmptyTCB;
    int i;
    
    if( gs_stTCBPoolManager.iUseCount == gs_stTCBPoolManager.iMaxCount )
    {
        return NULL;
    }

    for( i = 0 ; i < gs_stTCBPoolManager.iMaxCount ; i++ )
    {
        if( ( gs_stTCBPoolManager.pstStartAddress[ i ].stLink.qwID >> 32 ) == 0 )
        {
            pstEmptyTCB = &( gs_stTCBPoolManager.pstStartAddress[ i ] );
            break;
        }
    }

    pstEmptyTCB->stLink.qwID = ( ( QWORD ) gs_stTCBPoolManager.iAllocatedCount << 32 ) | i;
    gs_stTCBPoolManager.iUseCount++;
    gs_stTCBPoolManager.iAllocatedCount++;
    if( gs_stTCBPoolManager.iAllocatedCount == 0 )
    {
        gs_stTCBPoolManager.iAllocatedCount = 1;
    }
    
    return pstEmptyTCB;
}

static void kFreeTCB( QWORD qwID )
{
    int i;
    i = GETTCBOFFSET(qwID);
    
    kMemSet( &( gs_stTCBPoolManager.pstStartAddress[ i ].stContext ), 0, sizeof( CONTEXT ) );
    gs_stTCBPoolManager.pstStartAddress[ i ].stLink.qwID = i;
    
    gs_stTCBPoolManager.iUseCount--;
}

TCB* kCreateTask( QWORD qwFlags, void* pvMemoryAddress, QWORD qwMemorySize, QWORD qwEntryPointAddress )
{
    TCB* pstTask, * pstProcess;
    void* pvStackAddress;
    BOOL bPreviousFlag;

    bPreviousFlag = kLockForSystemData();
    pstTask = kAllocateTCB();
    if( pstTask == NULL )
    {
        kUnlockForSystemData(bPreviousFlag);
        return NULL;
    }
    
    pstProcess = kGetProcessByThread(kGetRunningTask());
    if(pstProcess == NULL)
    {
        kFreeTCB(pstTask->stLink.qwID);
        kUnlockForSystemData(bPreviousFlag);
        return NULL;
    }

    if(qwFlags & TASK_FLAGS_THREAD)
    {
        pstTask->qwParentProcessID = pstProcess->stLink.qwID;
        pstTask->pvMemoryAddress = pstProcess->pvMemoryAddress;
        pstTask->qwMemorySize = pstProcess->qwMemorySize;

        kAddListToTail(&(pstProcess->stChildThreadList), &(pstTask->stThreadLink));
    }
    else
    {
        pstTask->qwParentProcessID = pstProcess->stLink.qwID;
        pstTask->pvMemoryAddress = pvMemoryAddress;
        pstTask->qwMemorySize = qwMemorySize;
    }
    
    pstTask->stThreadLink.qwID = pstTask->stLink.qwID;

    kUnlockForSystemData(bPreviousFlag);

    pvStackAddress = ( void* ) ( TASK_STACKPOOLADDRESS + ( TASK_STACKSIZE * 
            GETTCBOFFSET( pstTask->stLink.qwID ) ) );
    
    kSetUpTask( pstTask, qwFlags, qwEntryPointAddress, pvStackAddress, 
            TASK_STACKSIZE );

    kInitializeList(&(pstTask->stChildThreadList));
    bPreviousFlag = kLockForSystemData();

    kAddTaskToReadyList( pstTask );

    //Lottery Scheduler
    if(qwFlags & TASK_FLAGS_HIGHEST){
        pstTask->ticket = 97;
        sum += 97;
    }
    else if(qwFlags & TASK_FLAGS_HIGH)
    {
        pstTask->ticket = 46;
        sum += 46;
    }
    else if(qwFlags & TASK_FLAGS_MEDIUM)
    {
        pstTask->ticket = 15;
        sum += 15;
    }
    else if(qwFlags & TASK_FLAGS_LOW)
    {
        pstTask->ticket = 5;
        sum += 5;
    }
    else if(qwFlags & TASK_FLAGS_LOWEST)
    {
        pstTask->ticket = 1;
        sum += 1;
    }
    /*
    //Stride Scheduler
    pstTask->pass = 0;
    pstTask->count = 0;
    if((int)qwFlags == TASK_FLAGS_MEDIUM)
        pstTask->stride = TOTALTICKET / 500;
    else if((int)qwFlags == TASK_FLAGS_LOW)
        pstTask->stride = TOTALTICKET / 1000;
    else if((int)qwFlags == TASK_FLAGS_LOWEST)
        pstTask->stride = TOTALTICKET / 10000;
    */

    kUnlockForSystemData(bPreviousFlag);

    return pstTask;
}

static void kSetUpTask( TCB* pstTCB, QWORD qwFlags, QWORD qwEntryPointAddress,
                 void* pvStackAddress, QWORD qwStackSize )
{
    kMemSet( pstTCB->stContext.vqRegister, 0, sizeof( pstTCB->stContext.vqRegister ) );
    
    pstTCB->stContext.vqRegister[ TASK_RSPOFFSET ] = ( QWORD ) pvStackAddress + 
            qwStackSize - 8;
    pstTCB->stContext.vqRegister[ TASK_RBPOFFSET ] = ( QWORD ) pvStackAddress + 
            qwStackSize - 8;

    *(QWORD *) ((QWORD) pvStackAddress + qwStackSize - 8) = (QWORD)kExitTask;

    pstTCB->stContext.vqRegister[ TASK_CSOFFSET ] = GDT_KERNELCODESEGMENT;
    pstTCB->stContext.vqRegister[ TASK_DSOFFSET ] = GDT_KERNELDATASEGMENT;
    pstTCB->stContext.vqRegister[ TASK_ESOFFSET ] = GDT_KERNELDATASEGMENT;
    pstTCB->stContext.vqRegister[ TASK_FSOFFSET ] = GDT_KERNELDATASEGMENT;
    pstTCB->stContext.vqRegister[ TASK_GSOFFSET ] = GDT_KERNELDATASEGMENT;
    pstTCB->stContext.vqRegister[ TASK_SSOFFSET ] = GDT_KERNELDATASEGMENT;
    
    pstTCB->stContext.vqRegister[ TASK_RIPOFFSET ] = qwEntryPointAddress;

    pstTCB->stContext.vqRegister[ TASK_RFLAGSOFFSET ] |= 0x0200;
    
    pstTCB->pvStackAddress = pvStackAddress;
    pstTCB->qwStackSize = qwStackSize;
    pstTCB->qwFlags = qwFlags;
}

void kInitializeScheduler( void )
{
    int i;
    TCB* pstTask;
    
    kInitializeTCBPool();

    for( i = 0 ; i < TASK_MAXREADYLISTCOUNT ; i++ )
    {
        kInitializeList( &( gs_stScheduler.vstReadyList[ i ] ) );
        gs_stScheduler.viExecuteCount[ i ] = 0;
    }    
    kInitializeList( &( gs_stScheduler.stWaitList ) );
    
    pstTask = kAllocateTCB();
    gs_stScheduler.pstRunningTask = pstTask;
    pstTask->qwFlags = TASK_FLAGS_HIGH | TASK_FLAGS_PROCESS | TASK_FLAGS_SYSTEM;
    pstTask->qwParentProcessID = pstTask->stLink.qwID;
    pstTask->pvMemoryAddress = (void*) 0x100000;
    pstTask->qwMemorySize = 0x500000;
    pstTask->pvStackAddress = (void *)0x600000;
    pstTask->qwStackSize = 0x100000;

    gs_stScheduler.qwSpendProcessorTimeInIdleTask = 0;
    gs_stScheduler.qwProcessorLoad = 0;
}

void kSetRunningTask( TCB* pstTask )
{
    BOOL bPreviousFlag;

    bPreviousFlag = kLockForSystemData();
    gs_stScheduler.pstRunningTask = pstTask;
    kUnlockForSystemData(bPreviousFlag);
}

TCB* kGetRunningTask( void )
{
    BOOL bPreviousFlag;
    TCB* pstRunningTask;

    bPreviousFlag = kLockForSystemData();
    pstRunningTask =  gs_stScheduler.pstRunningTask;
    kUnlockForSystemData(bPreviousFlag);

    return pstRunningTask;
}

static TCB* kGetNextTaskToRun( void )
{
    TCB* pstTarget = NULL, *pstRunningTask = kGetRunningTask();
    int iTaskCount, i, j;
    
    for( j = 0 ; j < 2 ; j++ )
    {
        for( i = 0 ; i < TASK_MAXREADYLISTCOUNT ; i++ )
        {
            iTaskCount = kGetListCount( &( gs_stScheduler.vstReadyList[ i ] ) );
            
            if( gs_stScheduler.viExecuteCount[ i ] < iTaskCount )
            {
                pstTarget = ( TCB* ) kRemoveListFromHeader(&( gs_stScheduler.vstReadyList[ i ] ) );
                gs_stScheduler.viExecuteCount[i]++;
                break;
            }
            else
            {
                gs_stScheduler.viExecuteCount[ i ] = 0;
            }
        }
    
        if( pstTarget != NULL )
        {
            break;
        }
    }
    return pstTarget;
}


//For Stride Scheduler
static TCB* kStrideNextToRun(){
    TCB* pstCurrent = NULL;
    int i, j;
    TCB* pstMinTask = NULL;

    for(i = 0; i < TASK_MAXREADYLISTCOUNT; i++)
    {
        pstCurrent = (TCB*)kGetHeaderFromList(&(gs_stScheduler.vstReadyList[i]));
        while(1)
        {
            if(pstCurrent == NULL)
                break;
            if(pstMinTask == NULL || pstCurrent->pass < pstMinTask->pass)
            {
                j = i;
                pstMinTask = pstCurrent;
            }
            if(pstCurrent == kGetTailFromList(&(gs_stScheduler.vstReadyList[i])))
                break;
            pstCurrent = (TCB*)kGetNextFromList(&gs_stScheduler.vstReadyList[i], pstCurrent);
            
        }
    }
    
    if(pstMinTask != NULL)
    {
        pstMinTask = (TCB*)kRemoveList(&(gs_stScheduler.vstReadyList[j]),pstMinTask->stLink.qwID);
        pstMinTask->pass += pstMinTask->stride;
    }

    return pstMinTask;
}

static BOOL kAddTaskToReadyList( TCB* pstTask )
{
    BYTE bPriority;
    
    bPriority = GETPRIORITY( pstTask->qwFlags );
    if( bPriority >= TASK_MAXREADYLISTCOUNT )
    {
        return FALSE;
    }
    
    kAddListToTail( &( gs_stScheduler.vstReadyList[ bPriority ] ), pstTask );
    return TRUE;
}

static TCB* kRemoveTaskFromReadyList( QWORD qwTaskID )
{
    TCB* pstTarget;
    BYTE bPriority;
    
    if( GETTCBOFFSET( qwTaskID ) >= TASK_MAXCOUNT )
    {
        return NULL;
    }

    pstTarget = &( gs_stTCBPoolManager.pstStartAddress[ GETTCBOFFSET( qwTaskID ) ] );
    if( pstTarget->stLink.qwID != qwTaskID )
    {
        return NULL;
    }
    
    bPriority = GETPRIORITY( pstTarget->qwFlags );

    pstTarget = kRemoveList( &( gs_stScheduler.vstReadyList[ bPriority ]), 
                     qwTaskID );
    return pstTarget;
}

BOOL kChangePriority( QWORD qwTaskID, BYTE bPriority )
{
    TCB* pstTarget;
    BOOL bPreviousFlag;
    
    if( bPriority > TASK_MAXREADYLISTCOUNT )
    {
        return FALSE;
    }
    bPreviousFlag = kLockForSystemData();
    
    pstTarget = gs_stScheduler.pstRunningTask;

    //Lottery Scheduler
    if( pstTarget->stLink.qwID == qwTaskID )
    {
        if(bPriority & TASK_FLAGS_HIGHEST){
            sum = sum - pstTarget->ticket;
            pstTarget->ticket = 97;
            sum += 97;
        }
        else if(bPriority & TASK_FLAGS_HIGH)
        {
            sum = sum - pstTarget->ticket;
            pstTarget->ticket = 46;
            sum += 46;
        }
        else if(bPriority & TASK_FLAGS_MEDIUM)
        {
            sum = sum - pstTarget->ticket;
            pstTarget->ticket = 15;
            sum += 15;
        }
        else if(bPriority & TASK_FLAGS_LOW)
        {
            sum = sum - pstTarget->ticket;
            pstTarget->ticket = 5;
            sum += 5;
        }
        else if(bPriority & TASK_FLAGS_LOWEST)
        {
            sum = sum - pstTarget->ticket;
            pstTarget->ticket = 1;
            sum += 1;
        }
        SETPRIORITY( pstTarget->qwFlags, bPriority );
    }
    else
    {
        pstTarget = kRemoveTaskFromReadyList( qwTaskID );
        if (pstTarget == NULL)
        {
            pstTarget = kGetTCBInTCBPool(GETTCBOFFSET(qwTaskID));
            if (pstTarget != NULL)
            {
                if (bPriority & TASK_FLAGS_HIGHEST)
                {
                    sum = sum - pstTarget->ticket;
                    pstTarget->ticket = 97;
                    sum += 97;
                }
                else if (bPriority & TASK_FLAGS_HIGH)
                {
                    sum = sum - pstTarget->ticket;
                    pstTarget->ticket = 46;
                    sum += 46;
                }
                else if (bPriority & TASK_FLAGS_MEDIUM)
                {
                    sum = sum - pstTarget->ticket;
                    pstTarget->ticket = 15;
                    sum += 15;
                }
                else if (bPriority & TASK_FLAGS_LOW)
                {
                    sum = sum - pstTarget->ticket;
                    pstTarget->ticket = 5;
                    sum += 5;
                }
                else if (bPriority & TASK_FLAGS_LOWEST)
                {
                    sum = sum - pstTarget->ticket;
                    pstTarget->ticket = 1;
                    sum += 1;
                }
                SETPRIORITY(pstTarget->qwFlags, bPriority);
            }
        }
        else
        {
            if (bPriority & TASK_FLAGS_HIGHEST)
            {
                sum = sum - pstTarget->ticket;
                pstTarget->ticket = 97;
                sum += 97;
            }
            else if (bPriority & TASK_FLAGS_HIGH)
            {
                sum = sum - pstTarget->ticket;
                pstTarget->ticket = 46;
                sum += 46;
            }
            else if (bPriority & TASK_FLAGS_MEDIUM)
            {
                sum = sum - pstTarget->ticket;
                pstTarget->ticket = 15;
                sum += 15;
            }
            else if (bPriority & TASK_FLAGS_LOW)
            {
                sum = sum - pstTarget->ticket;
                pstTarget->ticket = 5;
                sum += 5;
            }
            else if (bPriority & TASK_FLAGS_LOWEST)
            {
                sum = sum - pstTarget->ticket;
                pstTarget->ticket = 1;
                sum += 1;
            }
            SETPRIORITY(pstTarget->qwFlags, bPriority);
            kAddTaskToReadyList(pstTarget);
        }
    }
    kUnlockForSystemData(bPreviousFlag);
    return TRUE;

    /* BASIC
    if( pstTarget->stLink.qwID == qwTaskID )
    {
        SETPRIORITY( pstTarget->qwFlags, bPriority );
    }
    else
    {
        pstTarget = kRemoveTaskFromReadyList( qwTaskID );
        if( pstTarget == NULL )
        {
            pstTarget = kGetTCBInTCBPool( GETTCBOFFSET( qwTaskID ) );
            if( pstTarget != NULL )
            {
                SETPRIORITY( pstTarget->qwFlags, bPriority );
            }
        }
        else
        {
            SETPRIORITY( pstTarget->qwFlags, bPriority );
            kAddTaskToReadyList( pstTarget );
        }
    }
    kUnlockForSystemData(bPreviousFlag);
    return TRUE;*/
}

void kSchedule( void )
{
    TCB* pstRunningTask, * pstNextTask;
    BOOL bPreviousFlag;
    int i, j, iTaskCount;
    
    if( kGetReadyTaskCount() < 1 )
    {
        return ;
    }
    
    bPreviousFlag = kLockForSystemData();

    //Stride Schedule, Lottery Schedule, Round Robin Schedule
    //pstNextTask = kStrideNextToRun();
    //BASIC : pstNextTask = kGetNextTaskToRun();
    
    //Lottery
     gs_qwRandomValue = ranf() % sum + 1;
    iTaskCount = kGetReadyTaskCount();
    findT = 0;
    for(i = 0; i < iTaskCount; i++){
        pstNextTask = kGetNextTaskToRun();
        findT += pstNextTask->ticket;
        if (gs_qwRandomValue >= findT)
        {
            break;
        }
        else{
            kAddTaskToReadyList(pstNextTask);
        }
    }

    if( pstNextTask == NULL )
    {
        kUnlockForSystemData( bPreviousFlag );
        return ;
    }
    
    pstRunningTask = gs_stScheduler.pstRunningTask; 
    gs_stScheduler.pstRunningTask = pstNextTask;

    if( ( pstRunningTask->qwFlags & TASK_FLAGS_IDLE ) == TASK_FLAGS_IDLE )
    {
        gs_stScheduler.qwSpendProcessorTimeInIdleTask += 
            TASK_PROCESSORTIME - gs_stScheduler.iProcessorTime;
    }
    
    if( pstRunningTask->qwFlags & TASK_FLAGS_ENDTASK )
    {
        kAddListToTail( &( gs_stScheduler.stWaitList ), pstRunningTask );
        kSwitchContext( NULL, &( pstNextTask->stContext ) );
    }
    else
    {
        kAddTaskToReadyList( pstRunningTask );
        kSwitchContext( &( pstRunningTask->stContext ), &( pstNextTask->stContext ) );
    }

    gs_stScheduler.iProcessorTime = TASK_PROCESSORTIME;

    kUnlockForSystemData( bPreviousFlag );
}

BOOL kScheduleInInterrupt( void )
{
    TCB* pstRunningTask, * pstNextTask;
    char* pcContextAddress;
    BOOL bPreviousFlag;

    bPreviousFlag = kLockForSystemData();

    pstNextTask = kGetNextTaskToRun();
    if( pstNextTask == NULL )
    {
        kUnlockForSystemData();
        return FALSE;
    }
    
    pcContextAddress = ( char* ) IST_STARTADDRESS + IST_SIZE - sizeof( CONTEXT );
    
    pstRunningTask = gs_stScheduler.pstRunningTask;
    gs_stScheduler.pstRunningTask = pstNextTask;

    if( ( pstRunningTask->qwFlags & TASK_FLAGS_IDLE ) == TASK_FLAGS_IDLE )
    {
        gs_stScheduler.qwSpendProcessorTimeInIdleTask += TASK_PROCESSORTIME;
    }    
    
    if( pstRunningTask->qwFlags & TASK_FLAGS_ENDTASK )
    {    
        kAddListToTail( &( gs_stScheduler.stWaitList ), pstRunningTask );
    }
    else
    {
        kMemCpy( &( pstRunningTask->stContext ), pcContextAddress, sizeof( CONTEXT ) );
        kAddTaskToReadyList( pstRunningTask );
    }
    kUnlockForSystemData(bPreviousFlag);
    kMemCpy( pcContextAddress, &( pstNextTask->stContext ), sizeof( CONTEXT ) );
    
    return TRUE;
}

void kDecreaseProcessorTime( void )
{
    if( gs_stScheduler.iProcessorTime > 0 )
    {
        gs_stScheduler.iProcessorTime--;
    }
}

BOOL kIsProcessorTimeExpired( void )
{
    if( gs_stScheduler.iProcessorTime <= 0 )
    {
        return TRUE;
    }
    return FALSE;
}

BOOL kEndTask( QWORD qwTaskID )
{
    TCB* pstTarget;
    BYTE bPriority;
    BOOL bPreviousFlag;

    bPreviousFlag = kLockForSystemData();

    pstTarget = gs_stScheduler.pstRunningTask;
    if( pstTarget->stLink.qwID == qwTaskID )
    {
        pstTarget->qwFlags |= TASK_FLAGS_ENDTASK;
        SETPRIORITY( pstTarget->qwFlags, TASK_FLAGS_WAIT );
        
        kUnlockForSystemData(bPreviousFlag);
        kSchedule();
        
        while( 1 ) ;
    }
    else
    {
        pstTarget = kRemoveTaskFromReadyList( qwTaskID );
        if( pstTarget == NULL )
        {
            pstTarget = kGetTCBInTCBPool( GETTCBOFFSET( qwTaskID ) );
            if( pstTarget != NULL )
            {
                pstTarget->qwFlags |= TASK_FLAGS_ENDTASK;
                SETPRIORITY( pstTarget->qwFlags, TASK_FLAGS_WAIT );
            }
            kUnlockForSystemData(bPreviousFlag);
            return FALSE;
        }
        
        pstTarget->qwFlags |= TASK_FLAGS_ENDTASK;
        SETPRIORITY( pstTarget->qwFlags, TASK_FLAGS_WAIT );
        kAddListToTail( &( gs_stScheduler.stWaitList ), pstTarget );
    }
    return TRUE;
}

void kExitTask( void )
{
    kEndTask( gs_stScheduler.pstRunningTask->stLink.qwID );
}

int kGetReadyTaskCount( void )
{
    int iTotalCount = 0;
    int i;
    BOOL bPreviousFlag;

    bPreviousFlag = kLockForSystemData();

    for( i = 0 ; i < TASK_MAXREADYLISTCOUNT ; i++ )
    {
        iTotalCount += kGetListCount( &( gs_stScheduler.vstReadyList[ i ] ) );
    }
    
    kUnlockForSystemData(bPreviousFlag);
    return iTotalCount ;
}

int kGetTaskCount( void )
{
    int iTotalCount;
    BOOL bPreviousFlag;

    iTotalCount = kGetReadyTaskCount();
    bPreviousFlag = kLockForSystemData();
    iTotalCount += kGetListCount( &( gs_stScheduler.stWaitList ) ) + 1;

    kUnlockForSystemData(bPreviousFlag);
    return iTotalCount;
}

TCB* kGetTCBInTCBPool( int iOffset )
{
    if( ( iOffset < -1 ) && ( iOffset > TASK_MAXCOUNT ) )
    {
        return NULL;
    }
    
    return &( gs_stTCBPoolManager.pstStartAddress[ iOffset ] );
}

BOOL kIsTaskExist( QWORD qwID )
{
    TCB* pstTCB;
    
    pstTCB = kGetTCBInTCBPool( GETTCBOFFSET( qwID ) );
    if( ( pstTCB == NULL ) || ( pstTCB->stLink.qwID != qwID ) )
    {
        return FALSE;
    }
    return TRUE;
}

QWORD kGetProcessorLoad( void )
{
    return gs_stScheduler.qwProcessorLoad;
}

static TCB* kGetProcessByThread(TCB* pstThread)
{
    TCB* pstProcess;

    if(pstThread->qwFlags & TASK_FLAGS_PROCESS)
        return pstThread;

    pstProcess = kGetTCBInTCBPool(GETTCBOFFSET(pstThread->qwParentProcessID));

    if((pstProcess == NULL) || (pstProcess->stLink.qwID != pstThread->qwParentProcessID))
        return NULL;

    return pstProcess;
}

void kIdleTask( void )
{
    TCB* pstTask, * pstChildThread, * pstProcess;
    QWORD qwLastMeasureTickCount, qwLastSpendTickInIdleTask;
    QWORD qwCurrentMeasureTickCount, qwCurrentSpendTickInIdleTask;
    BOOL bPreviousFlag;
    int i, iCount;
    QWORD qwTaskID;
    void* pstThreadLink;
    
    qwLastSpendTickInIdleTask = gs_stScheduler.qwSpendProcessorTimeInIdleTask;
    qwLastMeasureTickCount = kGetTickCount();
    
    while( 1 )
    {
        qwCurrentMeasureTickCount = kGetTickCount();
        qwCurrentSpendTickInIdleTask = gs_stScheduler.qwSpendProcessorTimeInIdleTask;
        
        if( qwCurrentMeasureTickCount - qwLastMeasureTickCount == 0 )
        {
            gs_stScheduler.qwProcessorLoad = 0;
        }
        else
        {
            gs_stScheduler.qwProcessorLoad = 100 - 
                ( qwCurrentSpendTickInIdleTask - qwLastSpendTickInIdleTask ) * 
                100 /( qwCurrentMeasureTickCount - qwLastMeasureTickCount );
        }
        
        qwLastMeasureTickCount = qwCurrentMeasureTickCount;
        qwLastSpendTickInIdleTask = qwCurrentSpendTickInIdleTask;

        kHaltProcessorByLoad();
        
        if( kGetListCount( &( gs_stScheduler.stWaitList ) ) >= 0 )
        {
            while( 1 )
            {
                bPreviousFlag = kLockForSystemData();                
                pstTask = kRemoveListFromHeader( &( gs_stScheduler.stWaitList ) );
                if( pstTask == NULL )
                {
                    kUnlockForSystemData( bPreviousFlag );
                    break;
                }
                
                if( pstTask->qwFlags & TASK_FLAGS_PROCESS )
                {
                    iCount = kGetListCount( &( pstTask->stChildThreadList ) );
                    for( i = 0 ; i < iCount ; i++ )
                    {
                        pstThreadLink = ( TCB* ) kRemoveListFromHeader( 
                                &( pstTask->stChildThreadList ) );
                        if( pstThreadLink == NULL )
                        {
                            break;
                        }
                        
                        pstChildThread = GETTCBFROMTHREADLINK( pstThreadLink );

                        kAddListToTail( &( pstTask->stChildThreadList ),
                                &( pstChildThread->stThreadLink ) );

                        kEndTask( pstChildThread->stLink.qwID );
                    }
                    
                    if( kGetListCount( &( pstTask->stChildThreadList ) ) > 0 )
                    {
                        kAddListToTail( &( gs_stScheduler.stWaitList ), pstTask );
                        kUnlockForSystemData( bPreviousFlag );
                        continue;
                    }
                    else
                    {
                        
                    }
                }                
                else if( pstTask->qwFlags & TASK_FLAGS_THREAD )
                {
                    pstProcess = kGetProcessByThread( pstTask );
                    if( pstProcess != NULL )
                    {
                        kRemoveList( &( pstProcess->stChildThreadList ), pstTask->stLink.qwID );
                    }
                }
                
                qwTaskID = pstTask->stLink.qwID;
                kFreeTCB( qwTaskID );
                kUnlockForSystemData( bPreviousFlag );
                
                kPrintf( "IDLE: Task ID[0x%q] is completely ended.\n", 
                        qwTaskID );
            }
        }
        
        kSchedule();
    }
}

void kHaltProcessorByLoad( void )
{
    if( gs_stScheduler.qwProcessorLoad < 40 )
    {
        kHlt();
        kHlt();
        kHlt();
    }
    else if( gs_stScheduler.qwProcessorLoad < 80 )
    {
        kHlt();
        kHlt();
    }
    else if( gs_stScheduler.qwProcessorLoad < 95 )
    {
        kHlt();
    }
}