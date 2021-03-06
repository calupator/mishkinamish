﻿#include <Windows.h>
#include "KChFstate.h"
#include "Indicators.h"

volatile int KChFstate::state(0), KChFstate::minlevel(0), KChFstate::counter(0);
volatile bool KChFstate::flag_kc_anytime=false;
volatile int KChFstate::next_kc_counter=0; // Через сколько фреймов можно нажать  К/Ч (в упрощенном режиме)

volatile WORD KChFstate::key_to_press[6]={0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};
volatile char KChFstate::repeat_key[6]={0,0,0,0,0,0};
int KChFstate::key_state[6]={0,0,0,0,0,0}; // нажата-отжата
int KChFstate::key_cycle_counter[6]={0,0,0,0,0,0}; // Сколько ещё циклов нельзя менять состояние клавиши

//==================================================================================================
// // может ли к или Ч быть рассмотрена кандидатом на нажатие (в нужное ли время ?) mod. [28-DEC]
//==================================================================================================
bool KChFstate::IsKCValid()
{
	if(flag_kc_anytime) // упрощённый режим
	{
		if(0==next_kc_counter) // можно нажимать прямо в следующем фрейме
		{
			Indicators::KChConfirmed=1;
			next_kc_counter=20;
		}

		// всегда в упрощенном режиме
		return true;
	}
	else
	{
		// Есть мысль, что и в состоянии 3 всегда valid. Лишь бы уровень сигнала был подходящим. Меняем:
		// return ((2==state)||((3==state)&&(counter<=3));
		return ((2==state)||(3==state));
	}
}


//============================================================
// Пришёл новый фрейм и его уровень энергии подсчитан [18-dec]
//============================================================
void KChFstate::NewFrame(int energy_level)
{
	// [28-DEC] упрощённый режим
	if(flag_kc_anytime)
	{
		state=0;
		counter=0;

		// Декремент вызывается из потока WorkingThread, а проверка на 0 и выставление в 20 - в основном потоке 
		// Но атомарности ни там ни здесь не нужно, так как:
		// пропуск одного декремента не критичен, запоздание на один фрейм при нажатии некритично.
		if(next_kc_counter>0) next_kc_counter--;

		return; // чуть не забыл!
	}

	switch(state)
	{
	case 0:
		if(energy_level<minlevel) // не тот уровень мы считали нижним!
		{
			minlevel=energy_level;
			counter=0;
			return;
		}
		if(energy_level>minlevel+1) // снова не тот уровень мы считали базовым для спокойствия! но уже в сторону повышения.
		{
			minlevel=energy_level-1;
			counter=0;
			return;
		}
		// Остаёмся в нужных пределах
		counter++;
		if(counter>20)
		{
			counter=0;
			state=1;
		}
		return;

	case 1: // Здесь нам позволено получить всплеск энергии, но не позволено получить понижение энергии
		if(energy_level<minlevel) // не тот уровень мы считали нижним!
		{
			state=0;
			minlevel=energy_level;
			counter=0;
			return;
		}
		if(energy_level>minlevel+1) // мы выждали паузу и можем перейти в состояние 2 при всплеске
		{
#ifdef _DEBUG
			OutputDebugString(L"\r\n***NACHALO!***\r\n");
#endif
			state=2;
			counter=0;
			return;
		}
		// Если же ничего из вышеперечисленного не происходит, можем оставаться в этом состоянии хоть вечность. И счетчик нам не нужен.
		return;

	case 2:
		if(energy_level<=minlevel+1) // Это то, чего я ждал! Короткий звук завершился вовремя! Теперь ждём, что после него тоже будет пауза
		{
			state=3;
			counter=0;
			return;
		}
		counter++;
		// А вот находиться сдесь более 9 фреймов нельзя (восьмой уже пришёл ещё в состоянии 2)... 
		// Звук то короткий. Кто долго здесь находится, тот отсюда слетает...
		if(counter>8)
		{
			// Король на час не усидел больше положенного...
// !!! Здесь сбросить ожидающих "к" и "ч"
			Indicators::KChConfirmed=2;
#ifdef _DEBUG
			OutputDebugString(L"***dolgo :-( !***\r\n");
#endif
			state=0;
			counter=0;
			return;
		}
		return; // Единыжды это может быть выполнено. Второй раз - уже перебор фреймов короткого звука.

	case 3:
		if((energy_level>minlevel+1)&&counter>3) // Не выдержал паузы! Вслед за коротким звуком вплотную последовал другой. Валим отсюда (3 первых фрейма прощается).
		{
// !!! Здесь сбросить ожидающих "к" и "ч"
			Indicators::KChConfirmed=2;
			state=0;
			counter=0;
#ifdef _DEBUG
			OutputDebugString(L"\r\nLISHNY VSPLESK!\r\n");
#endif
			return;

		}
		counter++;
		if(counter>12)
		{
			// О, сладкий момент! Короткий звук пойман за хвост! Отсюда с почётом уходим в состояние 0
// !!! Здесь подтвердить ожидающих "к" и "ч"
			Indicators::KChConfirmed=1;
#ifdef _DEBUG
			OutputDebugString(L"\r\nSHORT SOUND!\r\n");
#endif
			state=0;
			counter=0;
			return;
		}
		return;
	}
	
}

//=========================================================================================
// Если вместо мыши нажимаем клавиши, то move обнуляется
// SendInput содран из Mhook
//=========================================================================================
LONG KChFstate::TryToPress(int i, LONG move)
{
	// Защита от дурака...
	if(i<0||i>6) return move;

	// Возможно, клавишу с номером i мы вообще не нажимаем...
	if(key_to_press[i]==0xffff) return move;

	// счётчик. при повторе меняет состояние клавиши каждые два тика (1/5 секунды)
	if(key_cycle_counter[i]>0) key_cycle_counter[i]--; else key_cycle_counter[i]=0; // Защита от случайного ухода в минус.

	// Всё-таки нажатие будет
	if((0==key_state[i])&&(0==key_cycle_counter[i])) //Нажимаем, если move больше граничного значения. Пусть это будет 3
	{
		if(move>3)
		{
			INPUT input={0};
			input.type=INPUT_KEYBOARD;
			input.ki.dwFlags = KEYEVENTF_SCANCODE;

			if(key_to_press[i]>0xFF) // Этот скан-код из двух байтов, где первый - E0
			{
				input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
			}

			input.ki.wScan=key_to_press[i];
			SendInput(1,&input,sizeof(INPUT));

			key_state[i]=1; // Клавиша нажата
			key_cycle_counter[i]=1; // Ещё (1) цикл её нельзя будет отжимать

			// [25-AUG-2017] при повторяющихся нажатиях рекурсивно вызываем сами себя
			//if(repeat_key[i])
			//	TryToPress(i, 0);
			
		}
	}
	else // Клавиша нажата
	{
		// Отжимаем её, только если вообще не было признаков этого звука (move==0) или [25-AUG-2017] пришло время отжатия для повтора
		if((0==move)||((repeat_key[i])&&(0==key_cycle_counter[i])))
		{
			INPUT input={0};
			input.type=INPUT_KEYBOARD;
			input.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;

			if(key_to_press[i]>0xFF) // Этот скан-код из двух байтов, где первый - E0
			{
				input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
			}

			input.ki.wScan=key_to_press[i];
			SendInput(1,&input,sizeof(INPUT));	

			key_state[i]=0; // Клавиша отжата
			key_cycle_counter[i]=1; // Ещё (1) цикл её нельзя будет нажимать
		}

	
	}
	return 0;
}

